import logging
from diffusers import logging as dlogging
import torch
import random
from diffusers import (
    StableDiffusionPipeline,
    StableDiffusionImg2ImgPipeline,
    StableDiffusionInpaintPipeline,
    DPMSolverMultistepScheduler,
)
import numpy as np
import re
from typing import List, Tuple
from PIL import Image
import cv2

logger = logging.getLogger("diffusers")

device = "cuda"
model_id = "runwayml/stable-diffusion-v1-5"


# ------------------------------------------------------------------------------
# Load base pipelines
# ------------------------------------------------------------------------------

pipe = StableDiffusionPipeline.from_pretrained(
    model_id,
    torch_dtype=torch.float16,
    use_safetensors=True,
    safety_checker=None,
).to(device)

img2img_pipe = StableDiffusionImg2ImgPipeline(
    vae=pipe.vae,
    text_encoder=pipe.text_encoder,
    tokenizer=pipe.tokenizer,
    unet=pipe.unet,
    scheduler=pipe.scheduler,
    safety_checker=None,
    feature_extractor=None,
).to(device)

# Inpaint pipeline for outpainting
inpaint_pipe = StableDiffusionInpaintPipeline(
    vae=pipe.vae,
    text_encoder=pipe.text_encoder,
    tokenizer=pipe.tokenizer,
    unet=pipe.unet,
    scheduler=pipe.scheduler,
    safety_checker=None,
    feature_extractor=None,
).to(device)


# ------------------------------------------------------------------------------
# Scheduler setup
# ------------------------------------------------------------------------------

for p in (pipe, img2img_pipe, inpaint_pipe):
    p.scheduler = DPMSolverMultistepScheduler.from_config(
        p.scheduler.config,
        use_karras_sigmas=True,
        algorithm_type="dpmsolver++",
    )
    p.scheduler.config.noise_offset = 0.02
    p.scheduler.config.sigma_min = 0.03

    p.enable_xformers_memory_efficient_attention()
    p.enable_attention_slicing()
    p.vae.enable_slicing()
    p.vae.enable_tiling()

dlogging.set_verbosity_info()


# ------------------------------------------------------------------------------
# Prompt weighting helpers (unchanged)
# ------------------------------------------------------------------------------

def _replace_innermost(text: str, open_ch: str, close_ch: str, weight: float):
    pattern = re.compile(rf"\{open_ch}([^\\{open_ch}\{close_ch}]*)\{close_ch}")
    m = pattern.search(text)
    if not m:
        return text, False
    inner = m.group(1).strip()
    if re.search(r":\s*[-+]?[0-9]*\.?[0-9]+$", inner):
        new_group = f"{open_ch}{inner}{close_ch}"
    else:
        new_group = f"({inner}:{weight:.6g})"
    new_text = text[:m.start()] + new_group + text[m.end():]
    return new_text, True


def _prompt_to_weighted_subprompts(prompt: str, base_emph: float = 1.1):
    txt = prompt
    parts = []
    stack = [1.0]
    cur = []
    i = 0
    emph = base_emph
    deemph = 1.0 / base_emph

    while i < len(txt):
        ch = txt[i]
        if ch == "(":
            if cur:
                parts.append(("".join(cur).strip(), stack[-1]))
                cur = []
            stack.append(stack[-1] * emph)
            i += 1
        elif ch == "[":
            if cur:
                parts.append(("".join(cur).strip(), stack[-1]))
                cur = []
            stack.append(stack[-1] * deemph)
            i += 1
        elif ch == ")" or ch == "]":
            if cur:
                parts.append(("".join(cur).strip(), stack[-1]))
                cur = []
            if len(stack) > 1:
                stack.pop()
            i += 1
        else:
            cur.append(ch)
            i += 1

    if cur:
        parts.append(("".join(cur).strip(), stack[-1]))

    final = []
    for text_part, w in parts:
        if not text_part:
            continue
        for chunk in re.split(r"\s*\|\s*|\s*,\s*", text_part):
            s = chunk.strip()
            if not s:
                continue
            m = re.match(r"^(.*?):\s*([-+]?[0-9]*\.?[0-9]+)\s*$", s)
            if m:
                final.append((m.group(1).strip(), float(m.group(2)) * w))
            else:
                final.append((s, w))
    return final


def _encode_subprompts_to_embeds(subprompts, tokenizer, text_encoder, device, max_length):
    device = torch.device(device)
    items = [(t.strip(), w) for t, w in subprompts if t and t.strip()]
    if not items:
        inputs = tokenizer("", padding=True, truncation=True, max_length=max_length, return_tensors="pt")
        input_ids = inputs.input_ids.to(device)
        attention_mask = inputs.attention_mask.to(device)
        with torch.no_grad():
            out = text_encoder(input_ids=input_ids, attention_mask=attention_mask)
        return out.last_hidden_state, attention_mask

    texts, weights = zip(*items)
    inputs = tokenizer(
        list(texts),
        padding="longest",
        truncation=True,
        max_length=max_length,
        return_tensors="pt",
    )
    input_ids = inputs.input_ids.to(device)
    attention_mask = inputs.attention_mask.to(device)

    with torch.no_grad():
        outputs = text_encoder(input_ids=input_ids, attention_mask=attention_mask)
        emb_batch = outputs.last_hidden_state

    batch_size, seq_len, dim = emb_batch.shape

    if any(float(w) != 1.0 for w in weights):
        w_tensor = torch.tensor(weights, dtype=emb_batch.dtype, device=emb_batch.device).view(batch_size, 1, 1)
        emb_batch = emb_batch * w_tensor

    trimmed_embeds = []
    trimmed_masks = []
    for i in range(batch_size):
        real_len = int(attention_mask[i].sum().item())
        if real_len == 0:
            real_len = 1
        emb_i = emb_batch[i:i+1, :real_len, :].contiguous()
        mask_i = attention_mask[i:i+1, :real_len].contiguous()
        trimmed_embeds.append(emb_i)
        trimmed_masks.append(mask_i)

    prompt_embeds = torch.cat(trimmed_embeds, dim=1)
    attention_mask = torch.cat(trimmed_masks, dim=1)

    return prompt_embeds.to(device), attention_mask.to(device)


def _build_prompt_embeds(pipe, prompt, negative_prompt="", base_emph=1.1):
    tokenizer = pipe.tokenizer
    text_encoder = pipe.text_encoder
    max_length = tokenizer.model_max_length

    conditional_subs = _prompt_to_weighted_subprompts(prompt, base_emph=base_emph)
    cond_embeds, cond_mask = _encode_subprompts_to_embeds(
        conditional_subs, tokenizer, text_encoder, pipe.device, max_length
    )

    if negative_prompt and negative_prompt.strip():
        negative_subs = _prompt_to_weighted_subprompts(negative_prompt, base_emph=base_emph)
        neg_embeds, neg_mask = _encode_subprompts_to_embeds(
            negative_subs, tokenizer, text_encoder, pipe.device, max_length
        )
    else:
        inputs = tokenizer("", padding="max_length", truncation=True, max_length=max_length, return_tensors="pt")
        input_ids = inputs.input_ids.to(pipe.device)
        attention_mask = inputs.attention_mask.to(pipe.device)
        with torch.no_grad():
            out = pipe.text_encoder(input_ids=input_ids, attention_mask=attention_mask)
        neg_embeds = out.last_hidden_state
        neg_mask = attention_mask

    return cond_embeds, cond_mask, neg_embeds, neg_mask


def _pad_embeds_and_masks(embeds_a, mask_a, embeds_b, mask_b, dtype=None, device=None):
    if dtype is None:
        dtype = embeds_a.dtype
    if device is None:
        device = embeds_a.device

    batch, seq_a, dim = embeds_a.shape
    _, seq_b, _ = embeds_b.shape
    target_seq = max(seq_a, seq_b)

    if seq_a < target_seq:
        pad_len = target_seq - seq_a
        pad_emb = torch.zeros((batch, pad_len, dim), dtype=dtype, device=device)
        embeds_a = torch.cat([embeds_a.to(device=device), pad_emb], dim=1)
        if mask_a is None:
            mask_a = torch.ones((batch, seq_a), dtype=torch.long, device=device)
        mask_pad = torch.zeros((batch, pad_len), dtype=mask_a.dtype, device=device)
        mask_a = torch.cat([mask_a.to(device=device), mask_pad], dim=1)
    else:
        embeds_a = embeds_a.to(device=device)
        if mask_a is not None:
            mask_a = mask_a.to(device=device)

    if seq_b < target_seq:
        pad_len = target_seq - seq_b
        pad_emb = torch.zeros((batch, pad_len, dim), dtype=dtype, device=device)
        embeds_b = torch.cat([embeds_b.to(device=device), pad_emb], dim=1)
        if mask_b is None:
            mask_b = torch.ones((batch, seq_b), dtype=torch.long, device=device)
        mask_pad = torch.zeros((batch, pad_len), dtype=mask_b.dtype, device=device)
        mask_b = torch.cat([mask_b.to(device=device), mask_pad], dim=1)
    else:
        embeds_b = embeds_b.to(device=device)
        if mask_b is not None:
            mask_b = mask_b.to(device=device)

    return embeds_a, mask_a, embeds_b, mask_b


# ------------------------------------------------------------------------------
# Original callback (unchanged)
# ------------------------------------------------------------------------------

def _callback(iter, i, t, extra_step_kwargs):
    """Original callback used for txt2img, img2img, and refiner."""
    if _check_interrupt():
        raise RuntimeError("Interrupt detected! Stopping generation.")

    latents = extra_step_kwargs.get("latents")
    if latents is None:
        return extra_step_kwargs

    with torch.no_grad():
        decoded = pipe.vae.decode(latents / 0.18215).sample
        if decoded.dim() == 4:
            image = (decoded / 2 + 0.5).clamp(0, 1)
            image = image.cpu().permute(0, 2, 3, 1).float().numpy()
            _send_image(image[-1])

    return extra_step_kwargs


# ------------------------------------------------------------------------------
# Outpaint-specific callback (shows full intermediate image)
# ------------------------------------------------------------------------------

def _make_outpaint_callback(vae):
    """
    Outpaint callback:
    - Shows the full intermediate image.
    - Displays real diffusion process (noise -> structure -> details).
    - Does not hide blur-padding or feather transitions.
    - Used ONLY inside outpaint.
    """
    def _cb(iter, i, t, extra_step_kwargs):
        if _check_interrupt():
            raise RuntimeError("Interrupt detected! Stopping generation.")
        latents = extra_step_kwargs.get("latents")
        if latents is None:
            return extra_step_kwargs

        with torch.no_grad():
            decoded = vae.decode(latents / 0.18215).sample
            if decoded.dim() == 4:
                img = (decoded / 2 + 0.5).clamp(0, 1)
                img = img.cpu().permute(0, 2, 3, 1).float().numpy()
                _send_image(img[-1])

        return extra_step_kwargs

    return _cb


# ------------------------------------------------------------------------------
# Blur padding + feather mask
# ------------------------------------------------------------------------------

def _make_blur_padding_and_mask(
    img_np,
    left_pad,
    right_pad,
    top_pad,
    bottom_pad,
    feather=30,
    min_alpha=0.2,
):
    h, w, _ = img_np.shape
    new_h = h + top_pad + bottom_pad
    new_w = w + left_pad + right_pad

    # ---------------------------------------------------------
    # 1. Background
    # ---------------------------------------------------------
    #bg = cv2.resize(img_np, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    #bg = cv2.GaussianBlur(bg, (9, 9), 0)
    #bg = cv2.GaussianBlur(bg, (51, 51), 0)
    #bg[top_pad:top_pad + h, left_pad:left_pad + w] = img_np



    canvas = np.zeros((new_h, new_w, 3), dtype=np.uint8)
    canvas[top_pad:top_pad + h, left_pad:left_pad + w] = img_np

    inpaint_mask = np.zeros((new_h, new_w), dtype=np.uint8)
    if left_pad > 0:
        inpaint_mask[:, :left_pad] = 255
    if right_pad > 0:
        inpaint_mask[:, new_w - right_pad:] = 255
    if top_pad > 0:
        inpaint_mask[:top_pad, :] = 255
    if bottom_pad > 0:
        inpaint_mask[new_h - bottom_pad:, :] = 255

    #mean_color = img_np.mean(axis=(0,1)).astype(np.uint8)

    #edge_strip=2

    #if left_pad > 0:
    #    canvas[:, :edge_strip] = mean_color
    #    inpaint_mask[:, :edge_strip] = 0   # не inpaint

    #if right_pad > 0:
    #    canvas[:, new_w - edge_strip:] = mean_color
    #    inpaint_mask[:, new_w - edge_strip:] = 0

    #if top_pad > 0:
    #    canvas[:edge_strip, :] = mean_color
    #    inpaint_mask[:edge_strip, :] = 0

    #if bottom_pad > 0:
    #    canvas[new_h - edge_strip:, :] = mean_color
    #    inpaint_mask[new_h - edge_strip:, :] = 0

    inpainted = cv2.inpaint(canvas, inpaint_mask, 8, cv2.INPAINT_TELEA)
    #inpainted = cv2.inpaint(canvas, inpaint_mask, 3, cv2.INPAINT_NS)

    #blur_bg = cv2.GaussianBlur(canvas, (9, 9), 0)
    #blur_bg = cv2.GaussianBlur(blur_bg, (51, 51), 0)

    #bg = (0.7 * inpainted + 0.3 * blur_bg).astype(np.uint8)
    
    bg = inpainted.astype(np.uint8)
    #bg[top_pad:top_pad + h, left_pad:left_pad + w] = img_np
    noise_sigma = 3
    noise = np.random.normal(0, noise_sigma, bg.shape).astype(np.int16)
    bg_noisy = np.clip(bg.astype(np.int16) + noise, 0, 255).astype(np.uint8)
    bg_noisy[top_pad:top_pad + h, left_pad:left_pad + w] = img_np
    bg = bg_noisy


    # ---------------------------------------------------------
    # 2. Mask (min_alpha!)
    # ---------------------------------------------------------
    mask = np.full((new_h, new_w), min_alpha, dtype=np.float32)

    # ---------------------------------------------------------
    # 3. Precompute coefficients
    # ---------------------------------------------------------
    feather = max(1, int(feather))
    inv_feather = 1.0 / feather
    pi = np.pi
    scale = (1.0 - min_alpha) * 0.5

    # ---------------------------------------------------------
    # 4. Cosine falloff
    # ---------------------------------------------------------
    def falloff(dist):
        if dist <= 0:
            return 1.0
        if dist >= feather:
            return min_alpha
        return min_alpha + scale * (1 + np.cos(pi * dist * inv_feather))

    # ---------------------------------------------------------
    # 5. Horizontal (left)
    # ---------------------------------------------------------
    if left_pad > 0:
        for x in range(left_pad + feather):
            dist = x - left_pad
            val = falloff(dist)
            mask[:, x] = np.maximum(mask[:, x], val)

    # ---------------------------------------------------------
    # 6. Horizontal (right)
    # ---------------------------------------------------------
    if right_pad > 0:
        for x in range(right_pad + feather):
            dist = x - right_pad
            val = falloff(dist)
            mask[:, new_w - 1 - x] = np.maximum(mask[:, new_w - 1 - x], val)

    # ---------------------------------------------------------
    # 7. Vertical (top)
    # ---------------------------------------------------------
    if top_pad > 0: 
        for y in range(top_pad + feather):
            dist = y - top_pad
            val = falloff(dist)
            mask[y, :] = np.maximum(mask[y, :], val)

    # ---------------------------------------------------------
    # 8. Vertical (bottom)
    # ---------------------------------------------------------
    if bottom_pad > 0:
        for y in range(bottom_pad + feather):
            dist = y - bottom_pad
            val = falloff(dist)
            mask[new_h - 1 - y, :] = np.maximum(mask[new_h - 1 - y, :], val)

    return bg, (mask * 255).astype(np.uint8)

# ------------------------------------------------------------------------------
# Outpaint expansion
# ------------------------------------------------------------------------------

def _pad_to_multiple(x, m):
    return (x + m - 1) // m * m

def _expand_with_inpaint(
    image,
    cond_embeds,
    cond_mask,
    neg_embeds,
    neg_mask,
    gen,
    expand_left_or_top: bool,
    expand_right_or_bottom: bool,
    expand_vertical: bool,
    steps: int = 20,
    cfg: float = 7.0,
):
    """
    Expands the image using inpainting:
    - Horizontal or vertical expansion.
    - Blur padding + feather mask.
    - Uses a dedicated outpaint callback.
    """

    if not expand_left_or_top and not expand_right_or_bottom:
        return image

    w, h = image.size
    img_np = np.array(image.convert("RGB"))

    MAX_AR = 4.0 / 3.0

    # Horizontal expansion
    if not expand_vertical:
        ar = w / h
        if ar >= MAX_AR:
            return image

        target_w = int(h * MAX_AR)
        #target_w = _pad_to_multiple(target_w, 64)
        add_total = max(0, target_w - w)
        if add_total == 0:
            return image

        if expand_left_or_top and expand_right_or_bottom:
            left_pad = add_total // 2
            right_pad = add_total - left_pad
        elif expand_left_or_top:
            left_pad = add_total
            right_pad = 0
        elif expand_right_or_bottom:
            left_pad = 0
            right_pad = add_total
        else:
            return image

        top_pad = bottom_pad = 0

    # Vertical expansion
    else:
        ar = h / w
        if ar >= MAX_AR:
            return image

        target_h = int(w * MAX_AR)
        #target_h = _pad_to_multiple(target_h, 64)
        add_total = max(0, target_h - h)
        if add_total == 0:
            return image

        if expand_left_or_top and expand_right_or_bottom:
            top_pad = add_total // 2
            bottom_pad = add_total - top_pad
        elif expand_left_or_top:
            top_pad = add_total
            bottom_pad = 0
        elif expand_right_or_bottom:
            top_pad = 0
            bottom_pad = add_total
        else:
            return image

        left_pad = right_pad = 0

    # Create padded background + feather mask
    bg, mask = _make_blur_padding_and_mask(img_np, left_pad, right_pad, top_pad, bottom_pad)

    bg = cv2.resize(bg, (w, h), interpolation=cv2.INTER_LINEAR)
    mask = cv2.resize(mask, (w, h), interpolation=cv2.INTER_LINEAR)

    _send_image(bg)

    padded_image = Image.fromarray(bg, mode="RGB")
    mask_image = Image.fromarray(mask, mode="L")


    #mask_np = np.array(mask_image)  # (H, W), uint8
    #mask_rgb = np.stack([mask_np, mask_np, mask_np], axis=-1)
    #_send_image(mask_rgb)

    # Outpaint callback
    outpaint_cb = _make_outpaint_callback(inpaint_pipe.vae)

    out = inpaint_pipe(
        prompt_embeds=cond_embeds,
        attention_mask=cond_mask,
        negative_prompt_embeds=neg_embeds,
        negative_attention_mask=neg_mask,
        image=padded_image,
        mask_image=mask_image,
        num_inference_steps=steps,
        guidance_scale=cfg,
        generator=gen,
        strength = 0.7,
        callback_on_step_end=outpaint_cb,
    )

    return out.images[0]


# ------------------------------------------------------------------------------
# Refiner pass (unchanged)
# ------------------------------------------------------------------------------

def _refine_image(
    image,
    cond_embeds,
    cond_mask,
    neg_embeds,
    neg_mask,
    gen,
    strength: float = 0.25,
    steps: int = 12,
    cfg: float = 5.0,
):
    image = image.convert("RGB")

    refined = img2img_pipe(
        prompt_embeds=cond_embeds,
        attention_mask=cond_mask,
        negative_prompt_embeds=neg_embeds,
        negative_attention_mask=neg_mask,
        image=image,
        strength=strength,
        guidance_scale=cfg,
        num_inference_steps=steps,
        generator=gen,
        callback_on_step_end=_callback,  # old callback stays
    )
    return refined.images[0]


# ---------------------------------------------------------
# Global cache for txt2img only
# ---------------------------------------------------------
_txt2img_cache = {
    "params": None,
    "image": None,
    "cond_embeds": None,
    "cond_mask": None,
    "neg_embeds": None,
    "neg_mask": None,
}


def generate_image(
    prompt: str,
    negative_prompt: str = "",
    guidance_scale: float = 7.5,
    num_steps: int = 50,
    seed: int = -1,
    use_refiner: bool = False,
    refiner_strength: float = 0.25,
    refiner_steps: int = 12,
    refiner_cfg: float = 5.0,
    expand_left_or_top: bool = False,
    expand_right_or_bottom: bool = False,
    expand_vertical: bool = False,
):
    """
    Main image generation:
    - txt2img (cached)
    - optional outpaint
    - optional refiner
    """

    global _txt2img_cache

    # ---------------------------------------------------------
    # 0. Seed
    # ---------------------------------------------------------
    if seed == -1:
        seed = random.randint(0, 2**31 - 1)
    logger.info(f'Using seed: {seed}')
    gen = torch.Generator(device=device).manual_seed(seed)

    # ---------------------------------------------------------
    # 1. Cache key ONLY for txt2img
    # ---------------------------------------------------------
    txt2img_key = (
        prompt,
        negative_prompt,
        guidance_scale,
        num_steps,
        seed,
    )

    # ---------------------------------------------------------
    # 2. Try using cached txt2img
    # ---------------------------------------------------------
    if _txt2img_cache["params"] == txt2img_key:
        logger.info("Using cached txt2img result")

        image = _txt2img_cache["image"].copy()
        cond_embeds = _txt2img_cache["cond_embeds"]
        cond_mask   = _txt2img_cache["cond_mask"]
        neg_embeds  = _txt2img_cache["neg_embeds"]
        neg_mask    = _txt2img_cache["neg_mask"]

    else:
        # ---------------------------------------------------------
        # 3. Build prompt embeddings
        # ---------------------------------------------------------
        cond_embeds, cond_mask, neg_embeds, neg_mask = _build_prompt_embeds(
            pipe,
            prompt,
            negative_prompt=negative_prompt,
            base_emph=1.05,
        )

        # Move embeddings to correct dtype/device
        cond_embeds = cond_embeds.to(dtype=pipe.text_encoder.dtype, device=pipe.device)
        neg_embeds = neg_embeds.to(dtype=pipe.text_encoder.dtype, device=pipe.device)
        cond_mask = cond_mask.to(pipe.device)
        neg_mask = neg_mask.to(pipe.device)

        # Pad embeddings so both conditional and negative have equal sequence length
        cond_embeds, cond_mask, neg_embeds, neg_mask = _pad_embeds_and_masks(
            cond_embeds, cond_mask, neg_embeds, neg_mask,
            dtype=cond_embeds.dtype, device=cond_embeds.device,
        )

        # ---------------------------------------------------------
        # 4. Base txt2img generation
        # ---------------------------------------------------------
        image = pipe(
            prompt_embeds=cond_embeds,
            attention_mask=cond_mask,
            negative_prompt_embeds=neg_embeds,
            negative_attention_mask=neg_mask,
            guidance_scale=guidance_scale,
            num_inference_steps=num_steps,
            generator=gen,
            callback_on_step_end=_callback,
        ).images[0]

        # ---------------------------------------------------------
        # 5. Save txt2img + embeddings to cache
        # ---------------------------------------------------------
        _txt2img_cache["params"] = txt2img_key
        _txt2img_cache["image"] = image.copy()
        _txt2img_cache["cond_embeds"] = cond_embeds
        _txt2img_cache["cond_mask"]   = cond_mask
        _txt2img_cache["neg_embeds"]  = neg_embeds
        _txt2img_cache["neg_mask"]    = neg_mask

    # ---------------------------------------------------------
    # 6. Optional outpainting
    # ---------------------------------------------------------
    if expand_left_or_top or expand_right_or_bottom:
        image = _expand_with_inpaint(
            image=image,
            cond_embeds=cond_embeds,
            cond_mask=cond_mask,
            neg_embeds=neg_embeds,
            neg_mask=neg_mask,
            gen=gen,
            expand_left_or_top=expand_left_or_top,
            expand_right_or_bottom=expand_right_or_bottom,
            expand_vertical=expand_vertical,
            steps=num_steps,
            cfg=guidance_scale,
        )

    # ---------------------------------------------------------
    # 7. Optional refiner
    # ---------------------------------------------------------
    if use_refiner:
        image = _refine_image(
            image=image,
            cond_embeds=cond_embeds,
            cond_mask=cond_mask,
            neg_embeds=neg_embeds,
            neg_mask=neg_mask,
            gen=gen,
            strength=refiner_strength,
            steps=refiner_steps,
            cfg=refiner_cfg,
        )

    # ---------------------------------------------------------
    # 8. Return final numpy array
    # ---------------------------------------------------------
    return np.array(image)

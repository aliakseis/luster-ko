"""Kornia image-processing adapter for luster-ko.

The application passes images as contiguous RGB NumPy arrays with shape
(H, W, 3) and dtype uint8.  Kornia/PyTorch normally works with tensors in
(N, C, H, W) format and floating point values in [0, 1].  This module keeps
that conversion private so every public function has the same simple
NumPy-in/NumPy-out interface as the other luster-ko scripts.

CUDA is used automatically when it is available.  The device is deliberately
kept as an optional argument on the public functions so the same script can
also be used on CPU-only installations.
"""

from __future__ import annotations

from typing import Tuple

import numpy as np
import torch
import kornia
import kornia.color
import kornia.enhance
import kornia.filters
import kornia.geometry.transform
import kornia.morphology


_DEVICE_CACHE: dict[str, torch.device] = {}


def _resolve_device(device: str = "auto") -> torch.device:
    """Resolve an application-friendly device name to a torch.device."""
    key = str(device).strip().lower()
    if key in _DEVICE_CACHE:
        return _DEVICE_CACHE[key]

    if key in ("", "auto"):
        result = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    elif key in ("cuda", "gpu"):
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested, but no CUDA device is available.")
        result = torch.device("cuda")
    elif key == "cpu":
        result = torch.device("cpu")
    else:
        # Permit normal torch device strings such as cuda:0.
        result = torch.device(key)
        if result.type == "cuda" and not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested, but no CUDA device is available.")

    _DEVICE_CACHE[key] = result
    return result


def _to_tensor(image: np.ndarray, device: str = "auto") -> torch.Tensor:
    """Convert luster-ko's HWC uint8 image to a 1xCxHxW float tensor."""
    if not isinstance(image, np.ndarray):
        raise TypeError("image must be a NumPy array")
    if image.ndim != 3 or image.shape[2] != 3:
        raise ValueError("image must have shape H*W*3")
    if image.dtype != np.uint8:
        raise ValueError("image must have dtype uint8")
    if image.size == 0:
        raise ValueError("image must not be empty")

    # from_numpy is zero-copy on the host.  contiguous() also handles arrays
    # returned by scripts that have non-standard strides.
    tensor = torch.from_numpy(np.ascontiguousarray(image))
    tensor = tensor.permute(2, 0, 1).unsqueeze(0)
    return tensor.to(device=_resolve_device(device), dtype=torch.float32) / 255.0


def _to_image(tensor: torch.Tensor) -> np.ndarray:
    """Convert a Kornia NCHW tensor back to luster-ko's HWC uint8 image."""
    if not isinstance(tensor, torch.Tensor):
        raise TypeError("Kornia operation did not return a torch.Tensor")
    if tensor.ndim != 4 or tensor.shape[0] != 1:
        raise ValueError("Expected a tensor with shape 1*C*H*W")
    if tensor.shape[1] != 3:
        raise ValueError("Expected a 3-channel RGB tensor")

    result = tensor.detach().clamp(0.0, 1.0)
    result = result[0].permute(1, 2, 0).contiguous()
    result = (result * 255.0).round().to(torch.uint8).cpu().numpy()
    return result


def _kernel_size(value: int, minimum: int = 1, odd: bool = True) -> Tuple[int, int]:
    """Validate and normalize a scalar kernel size for Kornia."""
    value = int(value)
    if value < minimum:
        raise ValueError(f"kernel_size must be >= {minimum}")
    if odd and value % 2 == 0:
        value += 1
    return value, value


def _sigma(value: float) -> Tuple[float, float]:
    value = float(value)
    if value <= 0.0:
        raise ValueError("sigma must be > 0")
    return value, value


def _run(image: np.ndarray, operation, device: str = "auto") -> np.ndarray:
    """Run a Kornia operation with inference mode and standard conversion."""
    tensor = _to_tensor(image, device)
    with torch.inference_mode():
        result = operation(tensor)
    return _to_image(result)


def gaussian_blur(image: np.ndarray, kernel_size: int = 5, sigma: float = 1.0,
                  device: str = "auto") -> np.ndarray:
    """Apply a Gaussian blur using Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd blur kernel size. Defaults to 5.
        sigma (float, optional): Gaussian standard deviation. Defaults to 1.0.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Blurred RGB uint8 image.
    """
    kernel = _kernel_size(kernel_size, 3)
    sig = _sigma(sigma)
    return _run(image, lambda x: kornia.filters.gaussian_blur2d(x, kernel, sig), device)


def box_blur(image: np.ndarray, kernel_size: int = 5, device: str = "auto") -> np.ndarray:
    """Apply a box/mean blur using Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd blur kernel size. Defaults to 5.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Blurred RGB uint8 image.
    """
    kernel = _kernel_size(kernel_size, 1)
    return _run(image, lambda x: kornia.filters.box_blur(x, kernel), device)


def median_blur(image: np.ndarray, kernel_size: int = 5, device: str = "auto") -> np.ndarray:
    """Apply a median filter using Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd filter kernel size. Defaults to 5.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Filtered RGB uint8 image.
    """
    kernel = _kernel_size(kernel_size, 3)
    return _run(image, lambda x: kornia.filters.median_blur(x, kernel), device)


def sharpen(image: np.ndarray, kernel_size: int = 5, sigma: float = 1.0,
            amount: float = 1.0, device: str = "auto") -> np.ndarray:
    """Sharpen an image with Kornia's unsharp-mask filter.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd Gaussian kernel size. Defaults to 5.
        sigma (float, optional): Gaussian standard deviation. Defaults to 1.0.
        amount (float, optional): Sharpening strength. Defaults to 1.0.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Sharpened RGB uint8 image.
    """
    kernel = _kernel_size(kernel_size, 3)
    sig = _sigma(sigma)
    amount = float(amount)
    if amount < 0.0:
        raise ValueError("amount must be >= 0")

    def operation(x):
        # Kornia's unsharp mask exposes sigma/kernel but not a strength on all
        # supported versions, so blend the generated mask explicitly.
        blurred = kornia.filters.gaussian_blur2d(x, kernel, sig)
        return x + amount * (x - blurred)

    return _run(image, operation, device)


def sobel_edges(image: np.ndarray, normalized: bool = True,
                device: str = "auto") -> np.ndarray:
    """Apply the Sobel gradient operator and return a three-channel edge image.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        normalized (bool, optional): Normalize the gradient magnitude. Defaults to True.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: RGB uint8 Sobel-gradient image.
    """
    def operation(x):
        gray = kornia.color.rgb_to_grayscale(x)
        gradient = kornia.filters.sobel(gray)
        magnitude = torch.sqrt((gradient * gradient).sum(dim=1, keepdim=True).clamp_min(0.0))
        if normalized:
            maximum = magnitude.amax(dim=(-2, -1), keepdim=True).clamp_min(1e-6)
            magnitude = magnitude / maximum
        return magnitude.repeat(1, 3, 1, 1)

    return _run(image, operation, device)


def laplacian_edges(image: np.ndarray, kernel_size: int = 5,
                    normalized: bool = True, device: str = "auto") -> np.ndarray:
    """Apply the Laplacian operator to produce an edge image.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd Laplacian kernel size. Defaults to 5.
        normalized (bool, optional): Normalize absolute response. Defaults to True.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: RGB uint8 Laplacian edge image.
    """
    kernel = _kernel_size(kernel_size, 3)

    def operation(x):
        gray = kornia.color.rgb_to_grayscale(x)
        response = kornia.filters.laplacian(gray, kernel[0])
        response = response.abs()
        if normalized:
            maximum = response.amax(dim=(-2, -1), keepdim=True).clamp_min(1e-6)
            response = response / maximum
        return response.repeat(1, 3, 1, 1)

    return _run(image, operation, device)


def canny_edges(image: np.ndarray, low_threshold: float = 0.1,
                high_threshold: float = 0.3, device: str = "auto") -> np.ndarray:
    """Detect Canny edges with Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        low_threshold (float, optional): Lower hysteresis threshold. Defaults to 0.1.
        high_threshold (float, optional): Upper hysteresis threshold. Defaults to 0.3.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: RGB uint8 Canny edge image.
    """
    low_threshold = float(low_threshold)
    high_threshold = float(high_threshold)
    if low_threshold < 0.0 or high_threshold < 0.0 or low_threshold > high_threshold:
        raise ValueError("thresholds must satisfy 0 <= low_threshold <= high_threshold")

    def operation(x):
        gray = kornia.color.rgb_to_grayscale(x)
        _, edges = kornia.filters.canny(gray, low_threshold, high_threshold)
        return edges.repeat(1, 3, 1, 1)

    return _run(image, operation, device)


def morphology_erode(image: np.ndarray, kernel_size: int = 3, device: str = "auto") -> np.ndarray:
    """Erode an RGB image using a square structuring element.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd structuring-element size. Defaults to 3.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Eroded RGB uint8 image.
    """
    kernel = torch.ones(_kernel_size(kernel_size, 1), dtype=torch.float32)
    return _run(image, lambda x: kornia.morphology.erosion(x, kernel.to(x.device)), device)


def morphology_dilate(image: np.ndarray, kernel_size: int = 3, device: str = "auto") -> np.ndarray:
    """Dilate an RGB image using a square structuring element.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd structuring-element size. Defaults to 3.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Dilated RGB uint8 image.
    """
    kernel = torch.ones(_kernel_size(kernel_size, 1), dtype=torch.float32)
    return _run(image, lambda x: kornia.morphology.dilation(x, kernel.to(x.device)), device)


def morphology_open(image: np.ndarray, kernel_size: int = 3, device: str = "auto") -> np.ndarray:
    """Apply morphological opening with a square structuring element.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd structuring-element size. Defaults to 3.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Morphologically opened RGB uint8 image.
    """
    kernel = torch.ones(_kernel_size(kernel_size, 1), dtype=torch.float32)
    return _run(image, lambda x: kornia.morphology.opening(x, kernel.to(x.device)), device)


def morphology_close(image: np.ndarray, kernel_size: int = 3, device: str = "auto") -> np.ndarray:
    """Apply morphological closing with a square structuring element.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        kernel_size (int, optional): Odd structuring-element size. Defaults to 3.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Morphologically closed RGB uint8 image.
    """
    kernel = torch.ones(_kernel_size(kernel_size, 1), dtype=torch.float32)
    return _run(image, lambda x: kornia.morphology.closing(x, kernel.to(x.device)), device)


def adjust_brightness(image: np.ndarray, factor: float = 1.0,
                      device: str = "auto") -> np.ndarray:
    """Adjust image brightness with Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        factor (float, optional): Brightness multiplier. 1.0 leaves the image unchanged.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Brightness-adjusted RGB uint8 image.
    """
    factor = float(factor)
    if factor < 0.0:
        raise ValueError("factor must be >= 0")
    return _run(image, lambda x: kornia.enhance.adjust_brightness(x, factor), device)


def adjust_contrast(image: np.ndarray, factor: float = 1.0,
                    device: str = "auto") -> np.ndarray:
    """Adjust image contrast with Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        factor (float, optional): Contrast multiplier. 1.0 leaves the image unchanged.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Contrast-adjusted RGB uint8 image.
    """
    factor = float(factor)
    if factor < 0.0:
        raise ValueError("factor must be >= 0")
    return _run(image, lambda x: kornia.enhance.adjust_contrast(x, factor), device)


def adjust_gamma(image: np.ndarray, gamma: float = 1.0, gain: float = 1.0,
                 device: str = "auto") -> np.ndarray:
    """Apply gamma correction with Kornia.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        gamma (float, optional): Gamma value. Defaults to 1.0.
        gain (float, optional): Multiplicative gain. Defaults to 1.0.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Gamma-adjusted RGB uint8 image.
    """
    gamma = float(gamma)
    gain = float(gain)
    if gamma <= 0.0:
        raise ValueError("gamma must be > 0")
    if gain < 0.0:
        raise ValueError("gain must be >= 0")
    return _run(image, lambda x: kornia.enhance.adjust_gamma(x, gamma, gain), device)


def rgb_to_grayscale(image: np.ndarray, device: str = "auto") -> np.ndarray:
    """Convert an RGB image to grayscale and replicate it to three channels.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Three-channel grayscale RGB uint8 image.
    """
    return _run(image, lambda x: kornia.color.rgb_to_grayscale(x).repeat(1, 3, 1, 1), device)


def normalize(image: np.ndarray, device: str = "auto") -> np.ndarray:
    """Normalize an image tensor using Kornia's per-channel normalization.

    The operation uses the image's own per-channel mean and standard deviation.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Locally normalized RGB uint8 image.
    """
    def operation(x):
        mean = x.mean(dim=(-2, -1), keepdim=True)
        std = x.std(dim=(-2, -1), keepdim=True).clamp_min(1e-6)
        return (x - mean) / std

    return _run(image, operation, device)


def resize_image(image: np.ndarray, width: int = 512, height: int = 512,
                  antialias: bool = True, device: str = "auto") -> np.ndarray:
    """Resize an image using Kornia's differentiable resize operation.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        width (int, optional): Output width in pixels. Defaults to 512.
        height (int, optional): Output height in pixels. Defaults to 512.
        antialias (bool, optional): Use antialiasing when downsampling. Defaults to True.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Resized RGB uint8 image.
    """
    width = int(width)
    height = int(height)
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be > 0")
    return _run(image, lambda x: kornia.geometry.transform.resize(x, (height, width), antialias=bool(antialias)), device)


def rotate_image(image: np.ndarray, angle: float = 0.0, device: str = "auto") -> np.ndarray:
    """Rotate an image around its center while preserving its dimensions.

    Args:
        image (np.ndarray): Input RGB uint8 image.
        angle (float, optional): Rotation angle in degrees, positive counter-clockwise.
        device (str, optional): "auto", "cpu", "cuda", or a torch device such as "cuda:0".

    Returns:
        np.ndarray: Rotated RGB uint8 image.
    """
    angle = float(angle)
    return _run(image, lambda x: kornia.geometry.transform.rotate(x, torch.tensor([angle], device=x.device)), device)

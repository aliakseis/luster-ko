
import cv2
import numpy as np


def _normalize_unit_sum(image: np.ndarray) -> np.ndarray:
    total = float(np.sum(image))
    if abs(total) > 1e-12:
        image = image / total
    return image


def _safe_normalize_minmax(image: np.ndarray) -> np.ndarray:
    mn = float(np.min(image))
    mx = float(np.max(image))
    d = mx - mn

    if d > 1e-12:
        return (image - mn) / d

    return np.zeros_like(image)


def _shift_psf(image: np.ndarray) -> np.ndarray:
    """Moves the PSF center to the top-left FFT origin."""
    cy, cx = image.shape[0] // 2, image.shape[1] // 2
    return np.roll(image, (-cy, -cx), axis=(0, 1))


def _compute_correlation_fft(
    gray: np.ndarray,
    radius: int
) -> np.ndarray:
    """Computes the normalized FFT autocorrelation matrix."""

    image = gray.astype(np.float32)
    image -= np.mean(image)

    gx = cv2.Sobel(image, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(image, cv2.CV_32F, 0, 1, ksize=3)

    magnitude = cv2.magnitude(gx, gy)
    magnitude = cv2.threshold(
        magnitude, 10.0, 0.0, cv2.THRESH_TOZERO
    )[1]

    spectrum = cv2.dft(
        magnitude,
        flags=cv2.DFT_COMPLEX_OUTPUT
    )

    correlation_spectrum = cv2.mulSpectrums(
        spectrum, spectrum, 0, conjB=True
    )

    correlation = cv2.idft(
        correlation_spectrum,
        flags=cv2.DFT_REAL_OUTPUT | cv2.DFT_SCALE
    )

    correlation = _shift_psf(correlation)

    cy, cx = correlation.shape[0] // 2, correlation.shape[1] // 2

    correlation = correlation[
        cy - radius:cy + radius + 1,
        cx - radius:cx + radius + 1
    ].copy()

    correlation = _safe_normalize_minmax(correlation)
    return _normalize_unit_sum(correlation)


def _clip_psf_by_heap(
    psf: np.ndarray,
    fraction: float = 0.8
) -> np.ndarray:
    """Removes the lowest fraction of PSF values."""

    values = psf.ravel()
    k = max(1, int(values.size * fraction))

    threshold = np.partition(values, k - 1)[k - 1]

    return np.maximum(psf - threshold, 0.0).astype(np.float32)


def _crop_psf_to_active_region(psf: np.ndarray) -> np.ndarray:
    """Crops zero-valued borders from the PSF."""

    active = np.any(psf != 0.0, axis=1)

    if not np.any(active):
        return psf.copy()

    top = np.argmax(active)
    bottom = len(active) - np.argmax(active[::-1])

    active = np.any(psf != 0.0, axis=0)

    left = np.argmax(active)
    right = len(active) - np.argmax(active[::-1])

    return psf[top:bottom, left:right].copy()


def _build_psf_from_matrix(matrix: np.ndarray) -> np.ndarray:
    psf = _clip_psf_by_heap(matrix)
    psf = _crop_psf_to_active_region(psf)
    return _normalize_unit_sum(psf)


def _get_minimal_gaussian_target(
    size: int,
    sigma: float = 0.85
) -> np.ndarray:
    center = size // 2

    y, x = np.mgrid[:size, :size].astype(np.float32)

    dx = x - center
    dy = y - center

    target = np.exp(
        -0.5 * (dx * dx + dy * dy) / (sigma * sigma)
    )

    return _normalize_unit_sum(target)


def _generate_mcs_filter_kernel(
    diffusion_matrix: np.ndarray,
    noise_reg: float = 0.005,
    kernel_size: int = 31
) -> np.ndarray:
    """Generates a regularized MCS inverse-filter kernel."""

    if kernel_size % 2 == 0:
        kernel_size += 1

    height, width = diffusion_matrix.shape

    if height > kernel_size or width > kernel_size:
        raise ValueError("Diffusion matrix is larger than the kernel.")

    target = np.zeros(
        (kernel_size, kernel_size), dtype=np.float32
    )

    pad_y = (kernel_size - height) // 2
    pad_x = (kernel_size - width) // 2

    target[
        pad_y:pad_y + height,
        pad_x:pad_x + width
    ] = diffusion_matrix.astype(np.float32)

    target = _normalize_unit_sum(target)

    reference = _get_minimal_gaussian_target(
        kernel_size, 0.85
    )

    # Move the kernel centers to the FFT origin.
    target_fft = cv2.dft(
        np.fft.ifftshift(target).astype(np.float32),
        flags=cv2.DFT_COMPLEX_OUTPUT
    )

    reference_fft = cv2.dft(
        np.fft.ifftshift(reference).astype(np.float32),
        flags=cv2.DFT_COMPLEX_OUTPUT
    )

    T = (
        target_fft[..., 0] +
        1j * target_fft[..., 1]
    )

    R = (
        reference_fft[..., 0] +
        1j * reference_fft[..., 1]
    )

    # H = T / R
    H = T * np.conj(R) / (np.abs(R) ** 2 + 1e-8)

    # Regularized Wiener-MCS inversion.
    K = np.conj(H) / (np.abs(H) ** 2 + noise_reg)

    K_complex = np.stack(
        (K.real, K.imag), axis=-1
    ).astype(np.float32)

    kernel_shifted = cv2.idft(
        K_complex,
        flags=cv2.DFT_SCALE | cv2.DFT_REAL_OUTPUT
    )

    # Restore the spatial center.
    kernel = np.fft.fftshift(kernel_shifted)

    return kernel.astype(np.float32)


def deblur_image(image: np.ndarray) -> np.ndarray:
    """Deblurs an image using the MCS autocorrelation-based method.

    The algorithm estimates the blur PSF from image autocorrelation,
    constructs a regularized inverse filter, and applies it to the
    luminance channel.

    Args:
        image (np.ndarray): Input grayscale or BGR image (uint8).

    Returns:
        np.ndarray: Deblurred image in the original color format.
    """

    if image is None or image.size == 0:
        raise ValueError("Input image is empty.")

    if image.dtype != np.uint8:
        raise ValueError("Input image must have dtype uint8.")

    radius = 15
    kernel_size = 31
    noise_reg = 0.005

    if image.ndim == 2:
        gray = image
        color = False
    elif image.ndim == 3 and image.shape[2] == 3:
        ycrcb = cv2.cvtColor(image, cv2.COLOR_BGR2YCrCb)
        gray = ycrcb[:, :, 0]
        color = True
    else:
        raise ValueError("Expected a grayscale or BGR image.")

    if min(gray.shape) < kernel_size:
        raise ValueError("Image dimensions must be at least 31x31.")

    # 1. Estimate the autocorrelation matrix.
    matrix = _compute_correlation_fft(gray, radius)

    # 2. Reconstruct the PSF.
    psf = _build_psf_from_matrix(matrix)

    # 3. Generate the regularized MCS filter.
    kernel = _generate_mcs_filter_kernel(
        psf,
        noise_reg=noise_reg,
        kernel_size=kernel_size
    )

    # 4. Apply the filter.
    result = cv2.filter2D(
        gray,
        cv2.CV_32F,
        kernel,
        anchor=(-1, -1),
        delta=0,
        borderType=cv2.BORDER_REPLICATE
    )

    result = np.clip(
        np.rint(result), 0, 255
    ).astype(np.uint8)

    # 5. Restore the original color representation.
    if color:
        ycrcb[:, :, 0] = result
        return cv2.cvtColor(ycrcb, cv2.COLOR_YCrCb2BGR)

    return result

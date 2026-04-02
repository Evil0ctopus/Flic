from pathlib import Path

COMFY_API_URL = "http://127.0.0.1:8188"
BUILD_ROOT = "build/animations"
SD_MOUNT_PATH = "D:/Flic/animations/face/default"
IMAGE_SIZE = (240, 240)
MAX_PNG_SIZE_BYTES = 400 * 1024
DEFAULT_STYLE_PROMPT = (
    "soft glowing creature eye, friendly, expressive, cyan glow, "
    "slightly asymmetrical, smooth geometry, flux style"
)
DEFAULT_NEGATIVE_PROMPT = "text, watermark, logo, extra limbs, distortion, artifacts"

PACKAGE_ROOT = Path(__file__).resolve().parent
SPECS_ROOT = PACKAGE_ROOT / "animation_specs"
WORKFLOW_TEMPLATE_PATH = PACKAGE_ROOT / "flic_flux_eye_workflow.json"
BUILD_ROOT_PATH = Path(BUILD_ROOT)
SD_MOUNT_PATH_OBJ = Path(SD_MOUNT_PATH)

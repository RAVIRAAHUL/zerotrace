import subprocess
import os
from concurrent.futures import ThreadPoolExecutor, as_completed
import logging
from pathlib import Path

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)

def run_wiper(drive_num: int, vol_letter: str = "NONE", extra_args: list = None, exe_path: str = "zerotrace.exe"):
    """
    Run zerotrace.exe on a specified drive.

    Args:
        drive_num (int): Physical drive number (e.g., 1 for PhysicalDrive1).
        vol_letter (str): Volume letter (e.g., 'E') or 'NONE' if not applicable.
        extra_args (list): Additional arguments to pass to zerotrace.exe.
        exe_path (str): Path to zerotrace.exe executable.

    Returns:
        tuple: (drive_num, success: bool, output: str)
    """
    if extra_args is None:
        extra_args = []

    # Validate inputs
    if not isinstance(drive_num, int):
        logger.error(f"Invalid drive number: {drive_num} (must be an integer)")
        return drive_num, False, "Invalid drive number"
    if not isinstance(vol_letter, str):
        logger.error(f"Invalid volume letter: {vol_letter} (must be a string)")
        return drive_num, False, "Invalid volume letter"
    if not isinstance(extra_args, list):
        logger.error(f"Invalid extra_args: {extra_args} (must be a list)")
        return drive_num, False, "Invalid extra arguments"

    # Ensure zerotrace.exe exists
    if not Path(exe_path).is_file():
        logger.error(f"zerotrace.exe not found at {exe_path}")
        return drive_num, False, f"Executable not found: {exe_path}"

    cmd = [exe_path, str(drive_num), vol_letter] + extra_args
    logger.info(f"Starting wipe on PhysicalDrive{drive_num} (Volume {vol_letter}) with command: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            text=True,
            capture_output=True,
            timeout=3600  # 1-hour timeout to prevent hanging
        )
        success = result.returncode == 0
        output = result.stdout if result.stdout else ""
        if result.stderr:
            output += f"\nError: {result.stderr}"
        if success:
            logger.info(f"Finished wiping PhysicalDrive{drive_num}")
        else:
            logger.error(f"Wipe failed on PhysicalDrive{drive_num} (return code: {result.returncode})")
        return drive_num, success, output
    except subprocess.TimeoutExpired:
        logger.error(f"Wipe timed out on PhysicalDrive{drive_num}")
        return drive_num, False, "Process timed out"
    except Exception as e:
        logger.error(f"Failed on PhysicalDrive{drive_num}: {e}")
        return drive_num, False, str(e)

def confirm_wipe(drives):
    """
    Prompt user to confirm before wiping drives.
    """
    logger.warning("WARNING: Wiping drives is a destructive operation and cannot be undone!")
    logger.warning(f"Drives to be wiped: {[(d, v) for d, v in drives]}")
    response = input("Type 'YES' to proceed with wiping: ")
    return response.strip().upper() == "YES"

if __name__ == "__main__":
    # Define drives to wipe (drive_number, volume_letter)
    drives = [
        (4, "E"),   # PhysicalDrive1, Volume E
        (2, "NONE") # PhysicalDrive2, no volume
    ]

    # Path to zerotrace.exe (update this to the actual path if needed)
    ZEROTRACE_PATH = "a.exe"  # Replace with full path if not in PATH, e.g., r"C:\Tools\zerotrace.exe"

    # Extra options for zerotrace.exe
    extra_args = ["--test"]  # Use [] for full wipe

    # Confirm before proceeding
    if not confirm_wipe(drives):
        logger.error("Wipe operation cancelled by user")
        exit(1)

    # Check if drives list is empty
    if not drives:
        logger.error("No drives specified for wiping")
        exit(1)

    # Run wipe operations in parallel
    with ThreadPoolExecutor(max_workers=max(1, len(drives))) as executor:
        futures = [
            executor.submit(run_wiper, d, v, extra_args, ZEROTRACE_PATH)
            for d, v in drives
        ]
        # Collect and report results
        for future in as_completed(futures):
            drive_num, success, output = future.result()
            if success:
                logger.info(f"PhysicalDrive{drive_num} wiped successfully")
            else:
                logger.error(f"PhysicalDrive{drive_num} wipe failed")
            if output:
                logger.info(f"Output for PhysicalDrive{drive_num}:\n{output}")

    logger.info("All wipe jobs finished.")
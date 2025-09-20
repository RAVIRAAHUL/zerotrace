SIH 2025 – ZeroTrace (Secure Disk Purge Tool)

🎯 Problem Statement

When sensitive data is deleted from a drive using normal delete or format operations, it can still be recovered using forensic tools. This creates a major security risk for government, defense, healthcare, and financial institutions.

💡 Our Solution – ZeroTrace

ZeroTrace is a Windows-based secure disk purge utility built according to NIST 800-88 standards. It performs irreversible data sanitization on storage devices like HDDs, SSDs, and removable media.

✅ Key Features

NIST 800-88 Compliant Purge – 3-pass overwrite (0x00, 0xFF, Random).

Direct Disk Access – Works at raw sector level, bypassing filesystem.

Volume Lock + Dismount – Ensures no process can interfere during purge.

Cross-Platform Portability – Works with Windows API, extendable to Linux.

Failsafe Confirmation – Requires user to type CONFIRM before execution.


🛠️ Technical Workflow

1. Drive Selection – Detects and lists all available physical drives.


2. Volume Locking – Issues FSCTL_LOCK_VOLUME and FSCTL_DISMOUNT_VOLUME to safely unmount.


3. Raw Drive Access – Opens device \\.\PhysicalDriveN with exclusive access.


4. Overwrite Passes:

Pass 1 → Write 0x00

Pass 2 → Write 0xFF

Pass 3 → Write random data



5. Completion – Displays per-pass status and verifies progress.



🔒 Security Benefits

Eliminates residual data traces.

Mitigates risk of identity theft and data leaks.

Essential for e-waste disposal, defense declassification, and compliance audits.


🚀 Impact

Provides low-cost, open-source alternative to expensive commercial tools.

Enables government agencies & enterprises to sanitize disks before disposal.

Boosts digital security readiness in India.


📸 Demo Output

C:\SIH\ZeroTrace>a 1 E
NIST 800-88 Purge starting on \\.\PhysicalDrive1
Type CONFIRM to proceed: CONFIRM
[PASS 1] Pattern 0x00 complete (16 MB)
[PASS 2] Pattern 0xFF complete (16 MB)
[PASS 3] Pattern Random complete (16 MB)
Purge operation completed.

👨‍💻 Team Contribution

Research & Standards Mapping – Understanding NIST 800-88.

Windows Low-Level Programming – Using DeviceIoControl, CreateFile, raw writes.

Cross-Platform Feasibility – Planning porting to Linux using ioctl and /dev/sdX.


📌 Future Enhancements

GUI dashboard for non-technical users.

Verification using cryptographic hashing.

Multi-threaded parallel purge for faster execution.

Integration with enterprise IT asset disposal workflows.



---

ZeroTrace – Because "Delete" is never enough.


	

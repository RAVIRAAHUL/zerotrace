// usbinfo_simple.c
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "setupapi.lib")

int main(void) {
    HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed.\n");
        return 1;
    }

    SP_DEVINFO_DATA DeviceInfoData;
    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++) {
        char buffer[1024];
        if (SetupDiGetDeviceInstanceIdA(hDevInfo, &DeviceInfoData, buffer, sizeof(buffer), NULL)) {
            printf("Device %lu: %s\n", i, buffer);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return 0;
}

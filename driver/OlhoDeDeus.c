// language: C, file: driver/OlhoDeDeus.c, target: Windows 10/11 x64
// Minimal kernel raw-disk reader for the Olho de Deus engine. It exposes a
// control device; user mode sends a disk number, offset and length and gets
// bytes back. The driver is built against the WDK NuGet headers and is not
// loaded by the build script.

#include <ntddk.h>
#include <ntstrsafe.h>

#define DEVICE_NAME L"\\Device\\OlhoDeDeusKernel"
#define SYMLINK_NAME L"\\DosDevices\\OlhoDeDeusKernel"

#define IOCTL_EOG_READ_DISK \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, \
             FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_EOG_VERSION \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

typedef struct _EOG_READ_REQUEST {
    ULONG DiskNumber;
    ULONG Reserved0;
    ULONGLONG Offset;
    ULONG Length;
    ULONG Reserved1;
} EOG_READ_REQUEST, *PEOG_READ_REQUEST;

static NTSTATUS eog_create_close(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS eog_read_disk(PIRP irp, PIO_STACK_LOCATION stack) {
    if (stack->Parameters.DeviceIoControl.InputBufferLength <
        sizeof(EOG_READ_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    EOG_READ_REQUEST request;
    RtlCopyMemory(&request, irp->AssociatedIrp.SystemBuffer,
                  sizeof(request));
    const ULONG output_length =
        stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (request.Length == 0 || request.Length > output_length ||
        request.Length > 16u * 1024u * 1024u) {
        return STATUS_INVALID_PARAMETER;
    }
    WCHAR disk_path[64];
    NTSTATUS status = RtlStringCchPrintfW(disk_path, 64,
        L"\\??\\PhysicalDrive%lu", request.DiskNumber);
    if (!NT_SUCCESS(status)) return status;
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, disk_path);
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    HANDLE handle = NULL;
    IO_STATUS_BLOCK io_status;
    status = ZwCreateFile(&handle, GENERIC_READ | SYNCHRONIZE, &attributes,
        &io_status, NULL, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) return status;
    LARGE_INTEGER offset;
    offset.QuadPart = request.Offset;
    status = ZwReadFile(handle, NULL, NULL, NULL, &io_status,
        irp->AssociatedIrp.SystemBuffer, request.Length, &offset, NULL);
    ZwClose(handle);
    if (NT_SUCCESS(status)) irp->IoStatus.Information = request.Length;
    return status;
}

static NTSTATUS eog_control(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_EOG_READ_DISK:
        status = eog_read_disk(irp, stack);
        break;
    case IOCTL_EOG_VERSION:
        if (stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(ULONG)) {
            *(ULONG*)irp->AssociatedIrp.SystemBuffer = 1;
            irp->IoStatus.Information = sizeof(ULONG);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    default:
        break;
    }
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static VOID eog_unload(PDRIVER_OBJECT driver) {
    UNICODE_STRING symlink;
    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);
    if (driver->DeviceObject != NULL)
        IoDeleteDevice(driver->DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry) {
    UNREFERENCED_PARAMETER(registry);
    UNICODE_STRING device_name;
    RtlInitUnicodeString(&device_name, DEVICE_NAME);
    PDEVICE_OBJECT device = NULL;
    NTSTATUS status = IoCreateDevice(driver, 0, &device_name,
        FILE_DEVICE_UNKNOWN, 0, FALSE, &device);
    if (!NT_SUCCESS(status)) return status;
    device->Flags |= DO_BUFFERED_IO;
    UNICODE_STRING symlink;
    RtlInitUnicodeString(&symlink, SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &device_name);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }
    driver->MajorFunction[IRP_MJ_CREATE] = eog_create_close;
    driver->MajorFunction[IRP_MJ_CLOSE] = eog_create_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = eog_control;
    driver->DriverUnload = eog_unload;
    return STATUS_SUCCESS;
}

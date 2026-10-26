$ErrorActionPreference = 'Stop'
[ordered]@{
    capturedUtc = [DateTime]::UtcNow.ToString('o')
    identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    processSessionId = (Get-Process -Id $PID).SessionId
    cpu = @(Get-CimInstance Win32_Processor | Select-Object Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed)
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name,PNPDeviceID,DriverVersion,DriverDate,VideoProcessor)
    os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,TotalVisibleMemorySize,FreePhysicalMemory
    disks = @(Get-PSDrive -PSProvider FileSystem | Where-Object Name -In @('D','E') | Select-Object Name,Used,Free)
    archiveDrivePresent = [IO.Directory]::Exists('E:\')
    competingProjectProcesses = @(Get-Process concord_runtime,clang-cl,lld-link,ninja,cmake -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,Path,SessionId)
    persistentEnvironmentChanged = $false
} | ConvertTo-Json -Depth 8

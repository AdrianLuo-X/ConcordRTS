#pragma once

class RuntimeInternetServiceHost;
struct RuntimeInternetServiceHostConfig;
struct RuntimeInternetServiceHostDeploymentConfig;

// Runs this service's existing host/console loop synchronously. The deployment
// and its certificate resources remain owned by the caller until this returns.
int runRuntimeInternetServiceProcess(
    RuntimeInternetServiceHost& host,
    RuntimeInternetServiceHostConfig config,
    const RuntimeInternetServiceHostDeploymentConfig& deployment
);

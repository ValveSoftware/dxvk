#ifdef __cplusplus
extern "C" {
#endif

    struct DXVKOptions {
        int32_t customVendorId;
        int32_t customDeviceId;
        int32_t nvapiHack;
    };

    DLLEXPORT HRESULT __stdcall DXVKGetOptions(struct DXVKOptions *out_opts);

#ifdef __cplusplus
}
#endif

#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <libsddc.h>
#include <thread>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "rx888_source",
    /* Description:     */ "RX888 source module for SDR++",
    /* Author:          */ "Dima",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const double sampleRates[] = {
    8000000,
    16000000,
    32000000,
    64000000,
    128000000
};

const char* sampleRatesTxt[] = {
    "8MHz",
    "16MHz",
    "32MHz",
    "64MHz",
    "128MHz"
};

const char* directSamplingModesTxt = "Disabled\0Enabled\0";

class RX888SourceModule : public ModuleManager::Instance {
public:
    RX888SourceModule(std::string name) {
        this->name = name;

        serverMode = false;

        sampleRate = sampleRates[3]; // Default to 64MHz

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        for (int i = 0; i < 5; i++) {
            sampleRateListTxt += sampleRatesTxt[i];
            sampleRateListTxt += '\0';
        }

        refresh();

        config.acquire();
        if (!config.conf["device"].is_string()) {
            selectedDevName = "";
            config.conf["device"] = "";
        }
        else {
            selectedDevName = config.conf["device"];
        }
        config.release(true);
        selectByName(selectedDevName);

        sigpath::sourceManager.registerSource("RX888", &handler);
    }

    ~RX888SourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("RX888");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {
        devNames.clear();
        devListTxt = "";

        devCount = sddc_get_device_count();
        char buf[1024];
        char venBuf[256];
        char prodBuf[256];
        char snBuf[256];
        for (int i = 0; i < devCount; i++) {
            // Gather device info
            const char* devName = sddc_get_device_name(i);
            int snErr = sddc_get_device_usb_strings(i, venBuf, prodBuf, snBuf);

            // Build name
            if (venBuf[0] && prodBuf[0]) {
                snprintf(buf, sizeof(buf), "%s %s [%s]##%d", venBuf, prodBuf, (!snErr && snBuf[0]) ? snBuf : "No Serial", i);
            }
            else {
                snprintf(buf, sizeof(buf), "%s [%s]##%d", devName ? devName : "RX888", (!snErr && snBuf[0]) ? snBuf : "No Serial", i);
            }

            // Add device to list
            devNames.push_back(buf);
            devListTxt += buf;
            devListTxt += '\0';
        }
    }

    void selectFirst() {
        if (devCount > 0) {
            selectById(0);
        }
    }

    void selectByName(std::string name) {
        for (int i = 0; i < devCount; i++) {
            if (name == devNames[i]) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        selectedDevName = devNames[id];

        int oret = sddc_open(&openDev, id);
        
        if (oret < 0) {
            selectedDevName = "";
            flog::error("Could not open RX888: {0}", oret);
            return;
        }

        // Initialize gain steps if supported by retrieving them
        const float* rfGainArray = nullptr;
        int nRf = sddc_get_rf_gain_steps(openDev, &rfGainArray);
        if (nRf > 0 && rfGainArray) {
            rfGainList = std::vector<float>(rfGainArray, rfGainArray + nRf);
        } else {
            rfGainList.clear();
        }

        const float* ifGainArray = nullptr;
        int nIf = sddc_get_if_gain_steps(openDev, &ifGainArray);
        if (nIf > 0 && ifGainArray) {
            ifGainList = std::vector<float>(ifGainArray, ifGainArray + nIf);
        } else {
            ifGainList.clear();
        }

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(selectedDevName)) {
            created = true;
            config.conf["devices"][selectedDevName]["sampleRate"] = 64000000.0;
            config.conf["devices"][selectedDevName]["directSampling"] = directSamplingMode;
            config.conf["devices"][selectedDevName]["biasT"] = biasT;
            config.conf["devices"][selectedDevName]["dither"] = dither;
            config.conf["devices"][selectedDevName]["pga"] = pga;
            config.conf["devices"][selectedDevName]["highz"] = highz;
            config.conf["devices"][selectedDevName]["rfGainId"] = rfGainId;
            config.conf["devices"][selectedDevName]["ifGainId"] = ifGainId;
        }

        // Load config
        if (config.conf["devices"][selectedDevName].contains("sampleRate")) {
            int selectedSr = config.conf["devices"][selectedDevName]["sampleRate"];
            for (int i = 0; i < 5; i++) {
                if (sampleRates[i] == selectedSr) {
                    srId = i;
                    sampleRate = selectedSr;
                    break;
                }
            }
        }

        if (config.conf["devices"][selectedDevName].contains("directSampling")) {
            directSamplingMode = config.conf["devices"][selectedDevName]["directSampling"];
        }

        if (config.conf["devices"][selectedDevName].contains("biasT")) {
            biasT = config.conf["devices"][selectedDevName]["biasT"];
        }

        if (config.conf["devices"][selectedDevName].contains("dither")) {
            dither = config.conf["devices"][selectedDevName]["dither"];
        }

        if (config.conf["devices"][selectedDevName].contains("pga")) {
            pga = config.conf["devices"][selectedDevName]["pga"];
        }

        if (config.conf["devices"][selectedDevName].contains("highz")) {
            highz = config.conf["devices"][selectedDevName]["highz"];
        }

        if (config.conf["devices"][selectedDevName].contains("rfGainId")) {
            rfGainId = config.conf["devices"][selectedDevName]["rfGainId"];
        }

        if (config.conf["devices"][selectedDevName].contains("ifGainId")) {
            ifGainId = config.conf["devices"][selectedDevName]["ifGainId"];
        }

        if (rfGainList.size() > 0 && rfGainId >= rfGainList.size()) { rfGainId = rfGainList.size() - 1; }
        if (ifGainList.size() > 0 && ifGainId >= ifGainList.size()) { ifGainId = ifGainList.size() - 1; }
        
        updateGainTxt();

        config.release(created);

        sddc_close(openDev);
    }

private:
    static void menuSelected(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("RX888SourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        flog::info("RX888SourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->selectedDevName == "") {
            flog::error("No device selected");
            return;
        }

        int oret = sddc_open(&_this->openDev, _this->devId);

        if (oret < 0) {
            flog::error("Could not open RX888");
            return;
        }

        flog::info("RX888 Sample Rate: {0}", _this->sampleRate);

        sddc_set_xtal_freq(_this->openDev, _this->sampleRate);
        sddc_set_center_freq64(_this->openDev, _this->freq);
        sddc_set_direct_sampling(_this->openDev, _this->directSamplingMode);
        sddc_enable_bias_tee(_this->openDev, _this->biasT ? 3 : 0);
        sddc_enable_adc_dither(_this->openDev, _this->dither ? 1 : 0);
        sddc_enable_adc_pga(_this->openDev, _this->pga ? 1 : 0);
        sddc_enable_hf_highz(_this->openDev, _this->highz ? 1 : 0);

        if (_this->rfGainList.size() > 0) {
            sddc_set_rf_gain(_this->openDev, _this->rfGainList[_this->rfGainId]);
        }
        if (_this->ifGainList.size() > 0) {
            sddc_set_if_gain(_this->openDev, _this->ifGainList[_this->ifGainId]);
        }

        _this->running = true;

        _this->workerThread = std::thread(&RX888SourceModule::worker, _this);

        flog::info("RX888SourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->stream.stopWriter();
        sddc_cancel_async(_this->openDev);
        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }
        _this->stream.clearWriteStop();
        sddc_close(_this->openDev);
        flog::info("RX888SourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (_this->running) {
            sddc_set_center_freq64(_this->openDev, freq);
        }
        _this->freq = freq;
        flog::info("RX888SourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_rx888_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectById(_this->devId);
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["device"] = _this->selectedDevName;
                config.release(true);
            }
        }

        if (SmGui::Combo(CONCAT("##_rx888_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) {
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_rx888_refr_", _this->name))) {
            _this->refresh();
            _this->selectByName(_this->selectedDevName);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Direct Sampling");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rx888_ds_", _this->name), &_this->directSamplingMode, directSamplingModesTxt)) {
            if (_this->running) {
                // Must stop and restart to change this safely per libsddc docs, 
                // but we will send command anyway or inform user.
                // Re-init may be needed
                flog::warn("Direct sampling change while running might not be supported. Try restarting the stream.");
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["directSampling"] = _this->directSamplingMode;
                config.release(true);
            }
        }

        if (_this->rfGainList.size() == 0) { SmGui::BeginDisabled(); }
        SmGui::LeftLabel("RF Gain");
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (ImGui::SliderInt(CONCAT("##_rx888_rfgain_", _this->name), &_this->rfGainId, 0, _this->rfGainList.size() - 1, _this->rfDbTxt)) {
            _this->updateGainTxt();
            if (_this->running) {
                sddc_set_rf_gain(_this->openDev, _this->rfGainList[_this->rfGainId]);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["rfGainId"] = _this->rfGainId;
                config.release(true);
            }
        }
        if (_this->rfGainList.size() == 0) { SmGui::EndDisabled(); }

        if (_this->ifGainList.size() == 0) { SmGui::BeginDisabled(); }
        SmGui::LeftLabel("IF Gain");
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (ImGui::SliderInt(CONCAT("##_rx888_ifgain_", _this->name), &_this->ifGainId, 0, _this->ifGainList.size() - 1, _this->ifDbTxt)) {
            _this->updateGainTxt();
            if (_this->running) {
                sddc_set_if_gain(_this->openDev, _this->ifGainList[_this->ifGainId]);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["ifGainId"] = _this->ifGainId;
                config.release(true);
            }
        }
        if (_this->ifGainList.size() == 0) { SmGui::EndDisabled(); }

        if (SmGui::Checkbox(CONCAT("Bias T##_rx888_biast_", _this->name), &_this->biasT)) {
            if (_this->running) {
                sddc_enable_bias_tee(_this->openDev, _this->biasT ? 3 : 0);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["biasT"] = _this->biasT;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("Dither##_rx888_dither_", _this->name), &_this->dither)) {
            if (_this->running) {
                sddc_enable_adc_dither(_this->openDev, _this->dither ? 1 : 0);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["dither"] = _this->dither;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("PGA##_rx888_pga_", _this->name), &_this->pga)) {
            if (_this->running) {
                sddc_enable_adc_pga(_this->openDev, _this->pga ? 1 : 0);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["pga"] = _this->pga;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("High-Z##_rx888_highz_", _this->name), &_this->highz)) {
            if (_this->running) {
                sddc_enable_hf_highz(_this->openDev, _this->highz ? 1 : 0);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["highz"] = _this->highz;
                config.release(true);
            }
        }
    }

    void worker() {
        if (sddc_read_async(openDev, asyncHandler, this) != 0) {
            flog::error("Failed to start async read");
            running = false;
        }
    }

    static void asyncHandler(const int16_t* buf, uint32_t count, void* ctx) {
        RX888SourceModule* _this = (RX888SourceModule*)ctx;
        if (count == 0) return;

        static bool firstCall = true;
        if (firstCall) {
            flog::info("RX888 asyncHandler triggered! Received buffer count={0}", count);
            firstCall = false;
        }

        uint32_t processed = 0;

        if (_this->directSamplingMode) {
            // Direct sampling mode: only I data
            while (processed < count) {
                if (_this->stream.writerStop) return;
                uint32_t toProcess = std::min<uint32_t>(count - processed, (uint32_t)STREAM_BUFFER_SIZE);
                for (uint32_t i = 0; i < toProcess; i++) {
                    _this->stream.writeBuf[i].re = ((float)buf[processed + i]) / 32768.0f;
                    _this->stream.writeBuf[i].im = 0.0f;
                }
                if (!_this->stream.swap(toProcess)) { return; }
                processed += toProcess;
            }
        } else {
            // Tuner mode: Interleaved I/Q
            uint32_t sampCount = count / 2;
            while (processed < sampCount) {
                if (_this->stream.writerStop) return;
                uint32_t toProcess = std::min<uint32_t>(sampCount - processed, (uint32_t)STREAM_BUFFER_SIZE);
                for (uint32_t i = 0; i < toProcess; i++) {
                    _this->stream.writeBuf[i].re = ((float)buf[(processed + i) * 2]) / 32768.0f;
                    _this->stream.writeBuf[i].im = ((float)buf[((processed + i) * 2) + 1]) / 32768.0f;
                }
                if (!_this->stream.swap(toProcess)) { return; }
                processed += toProcess;
            }
        }
    }

    void updateGainTxt() {
        if (rfGainList.size() > 0) {
            snprintf(rfDbTxt, sizeof(rfDbTxt), "%.1f dB", rfGainList[rfGainId]);
        }
        if (ifGainList.size() > 0) {
            snprintf(ifDbTxt, sizeof(ifDbTxt), "%.1f dB", ifGainList[ifGainId]);
        }
    }

    std::string name;
    sddc_dev_t* openDev;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    double sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedDevName = "";
    int devId = 0;
    int srId = 0;
    int devCount = 0;
    bool serverMode = false;
    std::thread workerThread;

    bool biasT = false;
    bool dither = false;
    bool pga = false;
    bool highz = false;

    int rfGainId = 0;
    std::vector<float> rfGainList;

    int ifGainId = 0;
    std::vector<float> ifGainList;

    int directSamplingMode = 0;

    char rfDbTxt[128] = "";
    char ifDbTxt[128] = "";

    std::vector<std::string> devNames;
    std::string devListTxt;
    std::string sampleRateListTxt;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = 0;
    config.setPath(core::args["root"].s() + "/rx888_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RX888SourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (RX888SourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

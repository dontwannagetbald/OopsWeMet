#include <SD.h>
#include <M5Unified.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <MFRC522_I2C.h>
#include <lgfx/utility/lgfx_pngle.h>
#include <esp_system.h>
#include "mbedtls/base64.h"

#define ENABLE_BLE_ENCOUNTER false
#if ENABLE_BLE_ENCOUNTER
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

#define SD_SPI_CS_PIN    4
#define SD_SPI_SCK_PIN   36
#define SD_SPI_MISO_PIN  35
#define SD_SPI_MOSI_PIN  37
#define DEFAULT_AUDIO_SAMPLE_RATE 16000
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define ANIMATION_REGION_Y 60
#define ANIMATION_REGION_H 180
#define DUEL_REGION_Y 64
#define DUEL_REGION_H 176
#define TRANSFER_FRAME_DELAY 13
#define TRANSFER_HOLD_MS 1400
#define TRANSFER_HOLD_FRAME "0010.png"
#define BUBBLE_LEFT_PATH "/bubble/left.png"
#define BUBBLE_RIGHT_PATH "/bubble/right.png"
#define GIFT_SEND1_PATH "/bubble/send1.png"
#define GIFT_RECEIVE1_PATH "/bubble/receive1.png"
#define GIFT_SEND2_PATH "/bubble/send2.png"
#define GIFT_RECEIVE2_PATH "/bubble/receive2.png"
#define GIFT_PRESENT_BG0_PATH "/present/background/000.png"
#define GIFT_PRESENT_BG1_PATH "/present/background/001.png"
#define GIFT_STILL_HOLD_MS 1800
#define GIFT_PRESENT_FRAME_DELAY 220
#define GIFT_PRESENT_LOOP_COUNT 5
#define DIALOG_TEXT_X 22
#define DIALOG_TEXT_Y 24
#define COFFEE_TEXT_X 20
#define COFFEE_TEXT_Y 24
#define COFFEE_TEXT_W 278
#define COFFEE_TEXT_H 30
#define CHAT_TEXT_SIZE 0.72f
#define TRANSFER_TEXT_SIZE 1.0f
#define UI_FONT_PRIMARY_PATH "/fonts/AaHuanMengKongJianXiangSuTi-24.vlw"
#define UI_FONT_FALLBACK_PATH "/fonts/AaHuanMengKongJianXiangSuTi-project-24.vlw"
#define NFC_I2C_ADDR 0x28
#define NFC_I2C_CLOCK 400000U
#define NFC_POLL_INTERVAL 120
#define NFC_TRIGGER_COOLDOWN_MS 1500
#define NFC_CARD_RELEASE_MS 2000
#define NFC_SUCCESS_FRAME_DELAY 0
#define NFC_SUCCESS_FRAME_STEP 2
#define NFC_FLASH_REPEAT_COUNT 3
#define NFC_FLASH_ON_MS 180
#define NFC_FLASH_OFF_MS 120
#define NFC_SCENE_OK_HOLD_MS 5000
#define ENABLE_HARDWARE_AUDIO false
#define DUEL_TEXT_WINDOW_CHARS 30
#define WIFI_SSID "huanyu-A95F"
#define WIFI_PASS "1234567890"
#define WS_HOST "192.168.1.199"
#define WS_PORT 8765
#define WS_PATH "/"

// ======================
// 状态
// ======================
String currentState = "idle";
String displayText = "你好";
String currentPersona = "zhang_zong";

String duelLeftPersona = "zhang_zong";
String duelRightPersona = "xiao_hong";
String duelSpeakerPersona = "zhang_zong";
String duelState = "idle";
String duelNextState = "";
String duelText = "";
bool duelMode = false;
bool duelOneShot = false;
bool sdReady = false;
String renderedSceneKey = "";
String cachedSceneKey = "";
bool sceneCanvasReady = false;
M5Canvas sceneCanvas(&M5.Display);
bool frameCanvasReady = false;
M5Canvas frameCanvas(&M5.Display);
bool mirrorCanvasReady = false;
M5Canvas mirrorCanvas(&M5.Display);
bool drawingToFrameCanvas = false;
bool displayUiFontLoaded = false;
bool frameUiFontLoaded = false;
const char* activeUiFontPath = nullptr;
MFRC522_I2C nfcReader(
    NFC_I2C_ADDR,
    -1,
    &Wire
);
MFRC522_I2C::MIFARE_Key nfcKey;
bool nfcReady = false;
uint8_t nfcReaderVersion = 0;
unsigned long lastNfcPollAt = 0;
unsigned long lastNfcHandledAt = 0;
unsigned long lastNfcSeenAt = 0;
String lastNfcUid = "";

bool isPlayingAudio = false;
uint8_t* activePcmBuffer = nullptr;
uint32_t activePcmSize = 0;
unsigned long activePcmEarliestDoneAt = 0;
unsigned long activePcmForceDoneAt = 0;
unsigned long frameDelay = 80;
bool enableAnimation = true;
unsigned long lastPersonaReportAt = 0;
unsigned long lastPowerReportAt = 0;
unsigned long lastTextScrollAt = 0;
uint16_t textScrollIndex = 0;
String lastScrollText = "";
String lastCoffeeRenderedText = "";
Preferences personaPrefs;
websockets::WebsocketsClient wsClient;
bool wsEnabled = false;
bool wsConnected = false;
bool wifiConnectedReported = false;
unsigned long lastWifiBeginAt = 0;
unsigned long lastWsAttemptAt = 0;
unsigned long lastWifiScanAt = 0;
bool giftPending = false;
bool giftPlaying = false;
bool giftStartsTransfer = false;
bool overlayCapturedBaseReady = false;
bool cleanFrameCaptureReady = false;
bool idleSelfFallbackTried = false;
uint8_t* wsPcmBuffer = nullptr;
uint32_t wsPcmSize = 0;
uint32_t wsPcmReceived = 0;
uint32_t wsPcmSampleRate = DEFAULT_AUDIO_SAMPLE_RATE;
bool wsPcmActive = false;
#if ENABLE_BLE_ENCOUNTER
BLEScan* bleScanner = nullptr;
BLEAdvertising* bleAdvertiser = nullptr;
bool bleReady = false;
String localBleName = "";
String bestPeerPersona = "";
String bestPeerName = "";
int bestPeerRssi = -127;
uint16_t bestPeerSeenCount = 0;
unsigned long bestPeerLastSeenAt = 0;
unsigned long lastBleScanAt = 0;
unsigned long lastBleAdvertiseAt = 0;
unsigned long lastEncounterReportAt = 0;
String lastEncounterPairKey = "";

const char* peerServiceUuid = "7f1d2b10-7b6a-4f5d-9a46-202605260001";
const char* peerNamePrefix = "REDNOTE-";
const uint16_t projectCompanyId = 0xFFFF;
const uint8_t projectProtocolVersion = 1;
const int encounterRssiThreshold = -62;
const int minBleRssi = -82;
const uint32_t bleScanIntervalMs = 4000;
const uint32_t blePeerFreshMs = 10000;
const uint32_t encounterReportCooldownMs = 30000;
#endif

// 当前帧
File currentDir;
bool folderOpened = false;
bool animationLooped = false;
File duelLeftDir;
File duelRightDir;
bool duelLeftOpened = false;
bool duelRightOpened = false;
bool duelLeftDone = false;
bool duelRightDone = false;
File transferDir;
bool transferOpened = false;
bool transferDone = false;
bool transferHoldActive = false;
unsigned long transferHoldStartedAt = 0;
String transferStageState = "";
String transferStageNextState = "";
String transferBackgroundState = "idle";

void playAnimationFrame();
void playDuelFrame();
void resetAnimationFolder();
void resetDuelFolders();
void processCommandLine(
    String msg
);
void handleWsPcmMessage(
    const String& msg
);
void playPCMBuffer(
    uint8_t* pcmBuffer,
    uint32_t pcmSize,
    uint32_t sampleRate,
    const char* source
);
void pumpAudioPlayback();
void handleDuelMessage(
    String msg
);
void wsBegin();
void wsLoop();
void beginWifiStation(
    bool resetRadio,
    const char* reason
);
void logWifiScanSummary();
void onWiFiEvent(
    WiFiEvent_t event,
    WiFiEventInfo_t info
);
bool loadDisplayUiFont();
bool loadFrameUiFont();
void applyUiFont(
    lgfx::LGFXBase& target,
    bool fontLoaded
);
void bleBegin();
void bleLoop();
void updateBleAdvertising();
void reportPowerStatus();
const char* resetReasonName(
    esp_reset_reason_t reason
);
void logHeap(
    const char* label
);
void nfcBegin();
bool nfcLoop();
bool pollNfcCard(
    String& uidHex,
    String& payload
);
bool readClassicNfcPayload(
    String& payload
);
bool readUltralightNfcPayload(
    String& payload
);
bool tryExtractSceneCommand(
    const uint8_t* data,
    size_t size,
    String& payload
);
bool handleNfcPayload(
    const String& payload
);
bool playNfcSuccessAnimation();
bool drawPngFileCentered(
    const String& path,
    bool clearBackground
);
bool drawPngFileFullScreen(
    const String& path,
    bool clearBackground
);
bool drawMirroredPngToTarget(
    lgfx::LGFXBase& target,
    const uint8_t* pngData,
    size_t pngSize
);
String resolvePresentPersonaPath(
    const String& persona
);
bool playGiftStillFrame(
    const String& path,
    uint32_t holdMs,
    bool clearBackground
);
bool playGiftPresentLoop(
    const String& persona,
    bool useCapturedBase
);
void playGiftSequence();
void clearGiftFlags();
String extractNfcCommandValue(
    const String& payload,
    const char* prefix
);
String extractNfcSceneName(
    const String& payload
);
String extractNfcPersonaName(
    const String& payload
);
String resolveNfcScenePath(
    const String& sceneName
);
String resolveNfcSceneOkPath(
    const String& sceneName
);
String nfcUidToHex();
void presentBlackFrame();
bool captureDisplayToSceneCanvas();
bool captureFrameCanvasToSceneCanvas();
void restoreCapturedSceneCanvas();
bool prepareCapturedOverlayBase();
void waitWithService(
    uint32_t durationMs
);
const char* wifiStatusName(
    wl_status_t status
);
bool personaDefaultsToLeft(
    const String& persona
);
bool isPenguinWitchPair();
bool shouldDisplayLeftPersona(
    const String& persona
);
bool shouldMirrorPersonaOnScreen(
    const String& persona
);
bool shouldUseLeftBubble(
    const String& speakerPersona
);
bool isChatLikeDuelState(
    const String& state
);
const char* bubblePathForSpeaker(
    const String& speakerPersona
);
void drawBubbleOverlay(
    lgfx::LGFXBase& target,
    const String& speakerPersona
);
void startDuelScene(
    const String& leftPersona,
    const String& rightPersona,
    const String& speakerPersona,
    const String& state,
    const String& text,
    bool oneShot,
    const String& nextState
);

String animationFolderPath(
    const String& persona,
    const String& state
) {

    String animationState =
        state == "coffee" ?
        "self" :
        state;

    if (
        state == "idle" &&
        persona ==
        "xiao_hong" &&
        idleSelfFallbackTried
    ) {
        animationState =
            "self";
    }

    return "/" +
        persona +
        "/animations/" +
        animationState;
}

bool isHiddenSdEntry(
    const String& filename
) {

    return filename.startsWith(
        "."
    ) ||
        filename.startsWith(
            "._"
        ) ||
        filename.indexOf(
            "/."
        ) >= 0 ||
        filename.indexOf(
            "/._"
        ) >= 0;
}

String scenePathFor(
    const String& persona,
    const String& state
) {

    if (
        state == "coffee"
    ) {
        return "/scenes/coffee_shop.png";
    }

    if (
        isChatLikeDuelState(
            state
        )
    ) {
        if (
            persona.indexOf(
                "zhang_zong"
            ) >= 0
        ) {
            return "/scenes/darkroom.png";
        }

        return "/scenes/park.png";
    }

    if (
        state == "idle" ||
        state == "outdoor" ||
        state == "leave"
    ) {
        return "/scenes/" +
            currentPersona +
            "_home.png";
    }

    return "/scenes/" +
        currentPersona +
        "_home.png";
}

String scenePersonaKey() {

    if (
        duelMode
    ) {
        return duelLeftPersona +
            "|" +
            duelRightPersona;
    }

    return currentPersona;
}

String resolveScenePath(
    const String& persona,
    const String& state
) {

    if (
        !sdReady
    ) {
        return "";
    }

    String primary =
        scenePathFor(
            scenePersonaKey(),
            state
        );

    if (
        SD.exists(
            primary
        )
    ) {
        return primary;
    }

    if (
        (
            !isChatLikeDuelState(
                state
            )
        ) &&
        SD.exists(
            "/scenes/home.png"
        )
    ) {
        return "/scenes/home.png";
    }

    if (
        SD.exists(
            "/scenes/park.png"
        )
    ) {
        return "/scenes/park.png";
    }

    if (
        SD.exists(
            "/scenes/darkroom.png"
        )
    ) {
        return "/scenes/darkroom.png";
    }

    return "";
}

void readPngSize(
    const uint8_t* buf,
    size_t size,
    uint32_t& imageWidth,
    uint32_t& imageHeight
) {

    imageWidth =
        320;

    imageHeight =
        240;

    if (
        size >= 24 &&
        buf[0] == 0x89 &&
        buf[1] == 'P' &&
        buf[2] == 'N' &&
        buf[3] == 'G'
    ) {
        imageWidth =
            ((uint32_t)buf[16] << 24) |
            ((uint32_t)buf[17] << 16) |
            ((uint32_t)buf[18] << 8) |
            (uint32_t)buf[19];

        imageHeight =
            ((uint32_t)buf[20] << 24) |
            ((uint32_t)buf[21] << 16) |
            ((uint32_t)buf[22] << 8) |
            (uint32_t)buf[23];
    }
}

struct MirroredPngReadState {
    const uint8_t* data;
    size_t size;
    size_t offset;
};

struct MirroredPngDrawState {
    lgfx::bgra8888_t* buffer;
    float scaleX;
    float scaleY;
};

static pngle_t* mirroredPngDecoder =
    nullptr;

static uint32_t mirroredPngReadCallback(
    void* userData,
    uint8_t* out,
    uint32_t len
) {

    MirroredPngReadState* state =
        (MirroredPngReadState*)userData;

    if (
        !state ||
        !state->data ||
        state->offset >= state->size
    ) {
        return 0;
    }

    size_t remaining =
        state->size -
        state->offset;

    size_t chunk =
        remaining < len ?
        remaining :
        len;

    memcpy(
        out,
        state->data +
        state->offset,
        chunk
    );

    state->offset +=
        chunk;

    return chunk;
}

static void mirroredPngDrawCallback(
    void* userData,
    uint32_t x,
    uint32_t y,
    uint_fast8_t div_x,
    size_t len,
    const uint8_t* argb
) {

    MirroredPngDrawState* state =
        (MirroredPngDrawState*)userData;

    if (
        !state ||
        !state->buffer
    ) {
        return;
    }

    int32_t destTop =
        ceilf(
            y *
            state->scaleY
        );

    int32_t destBottom =
        ceilf(
            (
                y + 1
            ) *
            state->scaleY
        );

    if (
        destTop < 0
    ) {
        destTop = 0;
    }

    if (
        destBottom >
        SCREEN_HEIGHT
    ) {
        destBottom =
            SCREEN_HEIGHT;
    }

    if (
        destTop >=
        destBottom
    ) {
        return;
    }

    uint32_t srcX =
        x;

    for (
        size_t i = 0;
        i < len;
        ++i
    ) {
        uint8_t alpha =
            argb[0];

        if (
            alpha > 0
        ) {
            int32_t left =
                ceilf(
                    srcX *
                    state->scaleX
                );

            int32_t right =
                ceilf(
                    (
                        srcX +
                        div_x
                    ) *
                    state->scaleX
                );

            if (
                left < 0
            ) {
                left = 0;
            }

            if (
                right >
                SCREEN_WIDTH
            ) {
                right =
                    SCREEN_WIDTH;
            }

            int32_t mirrorLeft =
                SCREEN_WIDTH -
                right;

            int32_t mirrorRight =
                SCREEN_WIDTH -
                left;

            if (
                mirrorLeft < 0
            ) {
                mirrorLeft = 0;
            }

            if (
                mirrorRight >
                SCREEN_WIDTH
            ) {
                mirrorRight =
                    SCREEN_WIDTH;
            }

            if (
                mirrorLeft <
                mirrorRight
            ) {
                lgfx::bgra8888_t pixel(
                    alpha,
                    argb[1],
                    argb[2],
                    argb[3]
                );

                for (
                    int32_t destY =
                        destTop;
                    destY < destBottom;
                    ++destY
                ) {
                    lgfx::bgra8888_t* row =
                        state->buffer +
                        (
                            destY *
                            SCREEN_WIDTH
                        );

                    for (
                        int32_t destX =
                            mirrorLeft;
                        destX <
                        mirrorRight;
                        ++destX
                    ) {
                        row[destX] =
                            pixel;
                    }
                }
            }
        }

        srcX +=
            div_x;
        argb +=
            4;
    }
}

bool drawMirroredPngToTarget(
    lgfx::LGFXBase& target,
    const uint8_t* pngData,
    size_t pngSize
) {

    void* mirrorBuffer =
        mirrorCanvas.getBuffer();

    if (
        !mirrorCanvasReady ||
        mirrorBuffer ==
            nullptr ||
        mirrorCanvas.bufferLength() <
            SCREEN_WIDTH *
            SCREEN_HEIGHT *
            4 ||
        !pngData ||
        pngSize == 0
    ) {
        mirrorCanvasReady =
            false;
        return false;
    }

    if (
        mirroredPngDecoder ==
        nullptr
    ) {
        mirroredPngDecoder =
            lgfx_pngle_new();
    }

    if (
        mirroredPngDecoder ==
        nullptr
    ) {
        return false;
    }

    MirroredPngReadState readState = {
        pngData,
        pngSize,
        0
    };

    if (
        lgfx_pngle_prepare(
            mirroredPngDecoder,
            mirroredPngReadCallback,
            &readState
        ) < 0
    ) {
        return false;
    }

    uint32_t imageWidth =
        lgfx_pngle_get_width(
            mirroredPngDecoder
        );

    uint32_t imageHeight =
        lgfx_pngle_get_height(
            mirroredPngDecoder
        );

    if (
        imageWidth == 0 ||
        imageHeight == 0
    ) {
        return false;
    }

    float scaleX =
        (float)SCREEN_WIDTH /
        (float)imageWidth;

    float scaleY =
        (float)SCREEN_HEIGHT /
        (float)imageHeight;

    memset(
        mirrorBuffer,
        0,
        mirrorCanvas.bufferLength()
    );

    MirroredPngDrawState drawState = {
        (lgfx::bgra8888_t*)
        mirrorBuffer,
        scaleX,
        scaleY
    };

    int result =
        lgfx_pngle_decomp(
            mirroredPngDecoder,
            mirroredPngDrawCallback
        );

    if (
        result < 0
    ) {
        return false;
    }

    target.pushAlphaImage(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (const lgfx::bgra8888_t*)
        mirrorBuffer
    );

    return true;
}

bool ensureSceneCanvas(
    const String& persona,
    const String& state
) {

    String scenePath =
        resolveScenePath(
            persona,
            state
        );

    if (
        scenePath.length() == 0
    ) {
        return false;
    }

    String sceneKey =
        persona +
        "|" +
        state +
        "|" +
        scenePath;

    if (
        sceneCanvasReady &&
        cachedSceneKey ==
        sceneKey
    ) {
        return true;
    }

    static String lastScenePath =
        "";

    if (
        lastScenePath !=
        scenePath
    ) {
        Serial.print(
            "Scene bg: "
        );
        Serial.println(
            scenePath
        );
        lastScenePath =
            scenePath;
    }

    File sceneFile =
        SD.open(
            scenePath
        );

    if (
        !sceneFile
    ) {
        return false;
    }

    size_t size =
        sceneFile.size();

    uint8_t* buf =
        (uint8_t*)malloc(
            size
        );

    if (
        !buf
    ) {
        sceneFile.close();
        return false;
    }

    sceneFile.read(
        buf,
        size
    );

    uint32_t imageWidth;
    uint32_t imageHeight;

    readPngSize(
        buf,
        size,
        imageWidth,
        imageHeight
    );

    float scaleX =
        imageWidth > 0 ?
        320.0f / (float)imageWidth :
        1.0f;

    float scaleY =
        imageHeight > 0 ?
        240.0f / (float)imageHeight :
        1.0f;

    bool canvasOk =
        sceneCanvasReady;

    if (
        canvasOk
    ) {
        sceneCanvas.fillScreen(
            BLACK
        );

        canvasOk =
            sceneCanvas.drawPng(
                buf,
                size,
                0,
                0,
                320,
                240,
                0,
                0,
                scaleX,
                scaleY
            );
    }

    bool ok =
        canvasOk;

    if (
        ok
    ) {
        cachedSceneKey =
            sceneKey;
    } else {
        cachedSceneKey =
            "";
    }

    free(
        buf
    );

    sceneFile.close();

    return ok;
}

bool drawSceneBackground(
    const String& persona,
    const String& state
) {

    if (
        !ensureSceneCanvas(
            persona,
            state
        )
    ) {
        renderedSceneKey =
            "";
        return false;
    }

    String sceneKey =
        persona +
        "|" +
        state +
        "|" +
        resolveScenePath(
            persona,
            state
        );

    sceneCanvas.pushSprite(
        0,
        0
    );

    renderedSceneKey =
        sceneKey;

    return true;
}

void drawIdleFallbackScreen(
    const String& persona,
    const String& state
) {

    M5.Display.fillScreen(
        0x39C7
    );

    M5.Display.setTextColor(
        WHITE
    );
    M5.Display.setFont(
        &fonts::Font4
    );
    M5.Display.setTextSize(
        1.0f
    );
    M5.Display.setCursor(
        24,
        84
    );
    M5.Display.print(
        persona
    );

    M5.Display.setFont(
        &fonts::Font2
    );
    M5.Display.setCursor(
        24,
        128
    );
    M5.Display.print(
        state
    );

    renderedSceneKey =
        "fallback|" +
        persona +
        "|" +
        state;
}

bool ensureSceneOnScreen(
    const String& persona,
    const String& state
) {

    String scenePath =
        resolveScenePath(
            persona,
            state
        );

    String sceneKey =
        persona +
        "|" +
        state +
        "|" +
        scenePath;

    if (
        renderedSceneKey ==
        sceneKey
    ) {
        return true;
    }

    if (
        drawSceneBackground(
            persona,
            state
        )
    ) {
        return true;
    }

    drawIdleFallbackScreen(
        persona,
        state
    );

    return false;
}

bool prepareFullFrameCanvas(
    const String& persona,
    const String& state
) {

    if (
        !frameCanvasReady
    ) {
        drawSceneBackground(
            persona,
            state
        );
        return false;
    }

    if (
        ensureSceneCanvas(
            persona,
            state
        )
    ) {
        sceneCanvas.pushSprite(
            &frameCanvas,
            0,
            0
        );
    } else {
        return false;
    }

    return true;
}

void presentFullFrameCanvas() {

    if (
        frameCanvasReady
    ) {
        frameCanvas.pushSprite(
            0,
            0
        );
        renderedSceneKey =
            "";
    }
}

void presentBlackFrame() {

    if (
        frameCanvasReady
    ) {
        frameCanvas.fillScreen(
            BLACK
        );
        frameCanvas.pushSprite(
            0,
            0
        );
    } else {
        M5.Display.fillScreen(
            BLACK
        );
    }

    resetRenderedScene();
}

bool captureDisplayToSceneCanvas() {

    if (
        !sceneCanvasReady ||
        sceneCanvas.getBuffer() ==
            nullptr
    ) {
        return false;
    }

    M5.Display.readRect(
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        (uint16_t*)
        sceneCanvas.getBuffer()
    );

    resetSceneCache();

    return true;
}

bool captureFrameCanvasToSceneCanvas() {

    if (
        !sceneCanvasReady ||
        sceneCanvas.getBuffer() ==
            nullptr ||
        !frameCanvasReady ||
        frameCanvas.getBuffer() ==
            nullptr
    ) {
        return false;
    }

    frameCanvas.pushSprite(
        &sceneCanvas,
        0,
        0
    );

    resetSceneCache();

    return true;
}

void restoreCapturedSceneCanvas() {

    if (
        !sceneCanvasReady ||
        sceneCanvas.getBuffer() ==
            nullptr
    ) {
        return;
    }

    sceneCanvas.pushSprite(
        0,
        0
    );

    if (
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr
    ) {
        sceneCanvas.pushSprite(
            &frameCanvas,
            0,
            0
        );
    }

    resetRenderedScene();
}

bool prepareCapturedOverlayBase() {

    if (
        !overlayCapturedBaseReady ||
        !sceneCanvasReady ||
        sceneCanvas.getBuffer() ==
            nullptr
    ) {
        return false;
    }

    if (
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr
    ) {
        sceneCanvas.pushSprite(
            &frameCanvas,
            0,
            0
        );
    } else {
        sceneCanvas.pushSprite(
            0,
            0
        );
    }

    resetRenderedScene();

    return true;
}

void waitWithService(
    uint32_t durationMs
) {

    unsigned long startedAt =
        millis();

    while (
        millis() - startedAt <
        durationMs
    ) {
        M5.update();
        wsLoop();
        bleLoop();
        delay(5);
    }
}

bool drawPngFileCentered(
    const String& path,
    bool clearBackground
) {

    if (
        !sdReady ||
        !SD.exists(path)
    ) {
        return false;
    }

    File file =
        SD.open(path);

    if (
        !file
    ) {
        return false;
    }

    size_t size =
        file.size();

    uint8_t* buf =
        (uint8_t*)malloc(
            size
        );

    if (
        !buf
    ) {
        file.close();
        return false;
    }

    file.read(
        buf,
        size
    );

    file.close();

    uint32_t imageWidth;
    uint32_t imageHeight;

    readPngSize(
        buf,
        size,
        imageWidth,
        imageHeight
    );

    float scaleX =
        imageWidth > SCREEN_WIDTH ?
        (float)SCREEN_WIDTH /
            (float)imageWidth :
        1.0f;

    float scaleY =
        imageHeight > SCREEN_HEIGHT ?
        (float)SCREEN_HEIGHT /
            (float)imageHeight :
        1.0f;

    float scale =
        scaleX < scaleY ?
        scaleX :
        scaleY;

    if (
        scale <= 0.0f
    ) {
        scale =
            1.0f;
    }

    int32_t drawWidth =
        (int32_t)(
            (float)imageWidth *
            scale
        );

    int32_t drawHeight =
        (int32_t)(
            (float)imageHeight *
            scale
        );

    if (
        drawWidth <= 0
    ) {
        drawWidth =
            SCREEN_WIDTH;
    }

    if (
        drawHeight <= 0
    ) {
        drawHeight =
            SCREEN_HEIGHT;
    }

    int32_t x =
        (
            SCREEN_WIDTH -
            drawWidth
        ) / 2;

    int32_t y =
        (
            SCREEN_HEIGHT -
            drawHeight
        ) / 2;

    bool drew =
        false;
    bool composeToCanvas =
        !clearBackground &&
        frameCanvasReady &&
        sceneCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr &&
        sceneCanvas.getBuffer() !=
            nullptr;

    if (
        frameCanvasReady
    ) {
        if (
            composeToCanvas
        ) {
            sceneCanvas.pushSprite(
                &frameCanvas,
                0,
                0
            );
        } else if (
            clearBackground
        ) {
            frameCanvas.fillScreen(
                BLACK
            );
        } else if (
            frameCanvas.getBuffer() !=
                nullptr
        ) {
            M5.Display.readRect(
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                (uint16_t*)
                frameCanvas.getBuffer()
            );
        }

        drew =
            frameCanvas.drawPng(
                buf,
                size,
                x,
                y,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                scale,
                scale
            );

        if (
            drew
        ) {
            frameCanvas.pushSprite(
                0,
                0
            );
        }
    } else {
        if (
            clearBackground
        ) {
            M5.Display.fillScreen(
                BLACK
            );
        }

        drew =
            M5.Display.drawPng(
                buf,
                size,
                x,
                y,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                scale,
                scale
            );
    }

    free(
        buf
    );

    if (
        drew
    ) {
        resetRenderedScene();
    }

    return drew;
}

bool drawPngPathToTargetFullScreen(
    lgfx::LGFXBase& target,
    const String& path
) {

    if (
        !sdReady ||
        !SD.exists(path)
    ) {
        return false;
    }

    File file =
        SD.open(path);

    if (
        !file
    ) {
        return false;
    }

    size_t size =
        file.size();

    uint8_t* buf =
        (uint8_t*)malloc(
            size
        );

    if (
        !buf
    ) {
        file.close();
        return false;
    }

    file.read(
        buf,
        size
    );

    file.close();

    bool drew =
        target.drawPng(
            buf,
            size,
            0,
            0,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            0,
            0,
            1.0f,
            1.0f
        );

    free(
        buf
    );

    return drew;
}

bool drawPngFileFullScreen(
    const String& path,
    bool clearBackground
) {

    bool drew =
        false;

    if (
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr
    ) {
        if (
            clearBackground
        ) {
            frameCanvas.fillScreen(
                BLACK
            );
        } else {
            M5.Display.readRect(
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                (uint16_t*)
                frameCanvas.getBuffer()
            );
        }

        drew =
            drawPngPathToTargetFullScreen(
                (lgfx::LGFXBase&)
                frameCanvas,
                path
            );

        if (
            drew
        ) {
            frameCanvas.pushSprite(
                0,
                0
            );
        }
    } else {
        if (
            clearBackground
        ) {
            M5.Display.fillScreen(
                BLACK
            );
        }

        drew =
            drawPngPathToTargetFullScreen(
                (lgfx::LGFXBase&)
                M5.Display,
                path
            );
    }

    if (
        drew
    ) {
        resetRenderedScene();
    }

    return drew;
}

String resolvePresentPersonaPath(
    const String& persona
) {

    if (
        persona.length() == 0 ||
        !sdReady
    ) {
        return "";
    }

    const String candidates[] = {
        "/present/" +
            persona +
            ".png",
        "/present/" +
            persona +
            ".PNG"
    };

    for (
        uint8_t i = 0;
        i < sizeof(candidates) / sizeof(candidates[0]);
        ++i
    ) {
        if (
            SD.exists(
                candidates[i]
            )
        ) {
            return candidates[i];
        }
    }

    return "";
}

bool playGiftStillFrame(
    const String& path,
    uint32_t holdMs,
    bool clearBackground
) {

    bool drew =
        false;

    if (
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr
    ) {
        if (
            clearBackground
        ) {
            frameCanvas.fillScreen(
                BLACK
            );
        } else if (
            !prepareCapturedOverlayBase()
        ) {
            frameCanvas.fillScreen(
                BLACK
            );
        }

        drew =
            drawPngPathToTargetFullScreen(
                (lgfx::LGFXBase&)
                frameCanvas,
                path
            );

        if (
            drew
        ) {
            frameCanvas.pushSprite(
                0,
                0
            );
        }
    } else {
        if (
            clearBackground
        ) {
            M5.Display.fillScreen(
                BLACK
            );
        } else if (
            !prepareCapturedOverlayBase()
        ) {
            M5.Display.fillScreen(
                BLACK
            );
        }

        drew =
            drawPngPathToTargetFullScreen(
                (lgfx::LGFXBase&)
                M5.Display,
                path
            );
    }

    if (
        !drew
    ) {
        return false;
    }

    resetRenderedScene();

    waitWithService(
        holdMs
    );

    return true;
}

bool playGiftPresentLoop(
    const String& persona,
    bool useCapturedBase
) {

    const String backgroundPaths[] = {
        GIFT_PRESENT_BG0_PATH,
        GIFT_PRESENT_BG1_PATH
    };

    String giftPath =
        resolvePresentPersonaPath(
            persona
        );

    bool playedAny =
        false;

    for (
        uint8_t loopIndex = 0;
        loopIndex <
        GIFT_PRESENT_LOOP_COUNT;
        ++loopIndex
    ) {
        for (
            uint8_t frameIndex = 0;
            frameIndex <
            sizeof(backgroundPaths) / sizeof(backgroundPaths[0]);
            ++frameIndex
        ) {
            bool drewFrame =
                false;

            if (
                frameCanvasReady &&
                frameCanvas.getBuffer() !=
                    nullptr
            ) {
                if (
                    useCapturedBase
                ) {
                    if (
                        !prepareCapturedOverlayBase()
                    ) {
                        frameCanvas.fillScreen(
                            BLACK
                        );
                    }
                } else {
                    frameCanvas.fillScreen(
                        BLACK
                    );
                }

                bool drewBackground =
                    drawPngPathToTargetFullScreen(
                        (lgfx::LGFXBase&)
                        frameCanvas,
                        backgroundPaths[frameIndex]
                    );

                bool drewGift =
                    false;

                if (
                    giftPath.length() > 0
                ) {
                    drewGift =
                        drawPngPathToTargetFullScreen(
                            (lgfx::LGFXBase&)
                            frameCanvas,
                            giftPath
                        );
                }

                drewFrame =
                    drewBackground ||
                    drewGift;

                if (
                    drewFrame
                ) {
                    frameCanvas.pushSprite(
                        0,
                        0
                    );
                }
            } else {
                if (
                    useCapturedBase
                ) {
                    if (
                        !prepareCapturedOverlayBase()
                    ) {
                        M5.Display.fillScreen(
                            BLACK
                        );
                    }
                } else {
                    M5.Display.fillScreen(
                        BLACK
                    );
                }

                bool drewBackground =
                    drawPngPathToTargetFullScreen(
                        (lgfx::LGFXBase&)
                        M5.Display,
                        backgroundPaths[frameIndex]
                    );

                bool drewGift =
                    false;

                if (
                    giftPath.length() > 0
                ) {
                    drewGift =
                        drawPngPathToTargetFullScreen(
                            (lgfx::LGFXBase&)
                            M5.Display,
                            giftPath
                        );
                }

                drewFrame =
                    drewBackground ||
                    drewGift;
            }

            if (
                drewFrame
            ) {
                playedAny =
                    true;

                resetRenderedScene();

                waitWithService(
                    GIFT_PRESENT_FRAME_DELAY
                );
            }
        }
    }

    return playedAny;
}

void playGiftSequence() {

    giftPlaying =
        true;

    bool useCapturedBase =
        giftStartsTransfer &&
        overlayCapturedBaseReady;

    if (
        !useCapturedBase
    ) {
        presentBlackFrame();
    }

    bool playedAny =
        false;

    playedAny =
        playGiftStillFrame(
            GIFT_SEND1_PATH,
            GIFT_STILL_HOLD_MS,
            !useCapturedBase
        ) ||
        playedAny;

    playedAny =
        playGiftStillFrame(
            GIFT_RECEIVE1_PATH,
            GIFT_STILL_HOLD_MS,
            !useCapturedBase
        ) ||
        playedAny;

    playedAny =
        playGiftPresentLoop(
            currentPersona,
            useCapturedBase
        ) ||
        playedAny;

    playedAny =
        playGiftStillFrame(
            GIFT_SEND2_PATH,
            GIFT_STILL_HOLD_MS,
            !useCapturedBase
        ) ||
        playedAny;

    playedAny =
        playGiftStillFrame(
            GIFT_RECEIVE2_PATH,
            GIFT_STILL_HOLD_MS,
            !useCapturedBase
        ) ||
        playedAny;

    if (
        !playedAny
    ) {
        Serial.println(
            "Gift sequence skipped"
        );
    }

    if (
        !giftStartsTransfer
    ) {
        presentBlackFrame();
    }

    giftPlaying =
        false;
}

void clearGiftFlags() {

    giftPending =
        false;

    giftPlaying =
        false;

    giftStartsTransfer =
        false;

    overlayCapturedBaseReady =
        false;

    cleanFrameCaptureReady =
        false;
}

String nfcUidToHex() {

    String uidHex =
        "";

    for (
        byte i = 0;
        i < nfcReader.uid.size;
        ++i
    ) {
        if (
            nfcReader.uid.uidByte[i] < 0x10
        ) {
            uidHex +=
                "0";
        }

        uidHex +=
            String(
                nfcReader.uid.uidByte[i],
                HEX
            );
    }

    uidHex.toUpperCase();

    return uidHex;
}

String extractNfcCommandValue(
    const String& payload,
    const char* prefix
) {

    String trimmed =
        payload;

    trimmed.trim();

    if (
        !prefix
    ) {
        return "";
    }

    size_t prefixLen =
        strlen(prefix);

    if (
        trimmed.length() <= prefixLen
    ) {
        return "";
    }

    String actualPrefix =
        trimmed.substring(
            0,
            prefixLen
        );

    actualPrefix.toUpperCase();

    String expectedPrefix =
        prefix;

    expectedPrefix.toUpperCase();

    if (
        actualPrefix !=
        expectedPrefix
    ) {
        return "";
    }

    String value =
        trimmed.substring(
            prefixLen
        );

    value.trim();

    String sanitized =
        "";

    for (
        uint16_t i = 0;
        i < value.length();
        ++i
    ) {
        char c =
            value[i];

        bool isAlpha =
            (
                c >= 'a' &&
                c <= 'z'
            ) ||
            (
                c >= 'A' &&
                c <= 'Z'
            );

        bool isDigit =
            c >= '0' &&
            c <= '9';

        if (
            isAlpha ||
            isDigit ||
            c == '_' ||
            c == '-'
        ) {
            sanitized +=
                c;
        } else {
            break;
        }
    }

    return sanitized;
}

String extractNfcSceneName(
    const String& payload
) {

    return extractNfcCommandValue(
        payload,
        "SCENE|"
    );
}

String extractNfcPersonaName(
    const String& payload
) {

    return extractNfcCommandValue(
        payload,
        "PERSONA|"
    );
}

bool tryExtractSceneCommand(
    const uint8_t* data,
    size_t size,
    String& payload
) {

    for (
        uint8_t prefixIndex = 0;
        prefixIndex < 2;
        ++prefixIndex
    ) {
        const char* prefix =
            prefixIndex == 0 ?
            "SCENE|" :
            "PERSONA|";
        const size_t prefixLen =
            strlen(prefix);

        for (
            size_t i = 0;
            i + prefixLen <= size;
            ++i
        ) {
            bool match =
                true;

            if (
                match
            ) {
                for (
                    size_t j = 0;
                    j < prefixLen;
                    ++j
                ) {
                    char c =
                        (char)data[i + j];

                    if (
                        c >= 'a' &&
                        c <= 'z'
                    ) {
                        c =
                            c - 'a' + 'A';
                    }

                    if (
                        c !=
                        prefix[j]
                    ) {
                        match =
                            false;
                        break;
                    }
                }
            }

            if (
                !match
            ) {
                continue;
            }

            String candidate =
                prefix;

            for (
                size_t k = i + prefixLen;
                k < size &&
                candidate.length() < 64;
                ++k
            ) {
                char c =
                    (char)data[k];

                bool isAlpha =
                    (
                        c >= 'a' &&
                        c <= 'z'
                    ) ||
                    (
                        c >= 'A' &&
                        c <= 'Z'
                    );

                bool isDigit =
                    c >= '0' &&
                    c <= '9';

                if (
                    isAlpha ||
                    isDigit ||
                    c == '_' ||
                    c == '-'
                ) {
                    candidate +=
                        c;
                } else {
                    break;
                }
            }

            String value =
                extractNfcCommandValue(
                    candidate,
                    prefix
                );

            if (
                value.length() > 0
            ) {
                payload =
                    String(prefix) +
                    value;
                return true;
            }
        }
    }

    return false;
}

bool readClassicNfcPayload(
    String& payload
) {

    uint8_t raw[256];
    size_t rawLen =
        0;

    for (
        byte block = 1;
        block <= 20 &&
        rawLen + 16 <= sizeof(raw);
        ++block
    ) {
        if (
            block % 4 == 3
        ) {
            continue;
        }

        byte authStatus =
            nfcReader.PCD_Authenticate(
                MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A,
                block,
                &nfcKey,
                &nfcReader.uid
            );

        if (
            authStatus !=
            MFRC522_I2C::STATUS_OK
        ) {
            continue;
        }

        byte buffer[18];
        byte bufferSize =
            sizeof(buffer);

        byte readStatus =
            nfcReader.MIFARE_Read(
                block,
                buffer,
                &bufferSize
            );

        nfcReader.PCD_StopCrypto1();

        if (
            readStatus !=
            MFRC522_I2C::STATUS_OK
        ) {
            continue;
        }

        memcpy(
            raw + rawLen,
            buffer,
            16
        );
        rawLen +=
            16;

        if (
            tryExtractSceneCommand(
                raw,
                rawLen,
                payload
            )
        ) {
            return true;
        }
    }

    return false;
}

bool readUltralightNfcPayload(
    String& payload
) {

    uint8_t raw[192];
    size_t rawLen =
        0;

    for (
        byte page = 4;
        page <= 36 &&
        rawLen + 16 <= sizeof(raw);
        page += 4
    ) {
        byte buffer[18];
        byte bufferSize =
            sizeof(buffer);

        byte readStatus =
            nfcReader.MIFARE_Read(
                page,
                buffer,
                &bufferSize
            );

        if (
            readStatus !=
            MFRC522_I2C::STATUS_OK
        ) {
            continue;
        }

        memcpy(
            raw + rawLen,
            buffer,
            16
        );
        rawLen +=
            16;

        if (
            tryExtractSceneCommand(
                raw,
                rawLen,
                payload
            )
        ) {
            return true;
        }
    }

    return false;
}

bool pollNfcCard(
    String& uidHex,
    String& payload
) {

    uidHex =
        "";
    payload =
        "";

    if (
        !nfcReader.PICC_IsNewCardPresent() ||
        !nfcReader.PICC_ReadCardSerial()
    ) {
        return false;
    }

    uidHex =
        nfcUidToHex();

    byte piccType =
        nfcReader.PICC_GetType(
            nfcReader.uid.sak
        );

    if (
        piccType ==
            MFRC522_I2C::PICC_TYPE_MIFARE_MINI ||
        piccType ==
            MFRC522_I2C::PICC_TYPE_MIFARE_1K ||
        piccType ==
            MFRC522_I2C::PICC_TYPE_MIFARE_4K
    ) {
        readClassicNfcPayload(
            payload
        );
    } else {
        readUltralightNfcPayload(
            payload
        );

        if (
            payload.length() == 0
        ) {
            readClassicNfcPayload(
                payload
            );
        }
    }

    nfcReader.PICC_HaltA();
    nfcReader.PCD_StopCrypto1();

    return true;
}

String resolveNfcScenePath(
    const String& sceneName
) {

    if (
        sceneName.length() == 0 ||
        !sdReady
    ) {
        return "";
    }

    const String candidates[] = {
        "/nfc/scenes/" +
            sceneName +
            ".png",
        "/nfc/scenes/" +
            sceneName +
            ".PNG",
        "/scenes/" +
            sceneName +
            ".png",
        "/scenes/" +
            sceneName +
            ".PNG"
    };

    for (
        uint8_t i = 0;
        i < sizeof(candidates) / sizeof(candidates[0]);
        ++i
    ) {
        if (
            SD.exists(
                candidates[i]
            )
        ) {
            return candidates[i];
        }
    }

    return "";
}

String resolveNfcSceneOkPath(
    const String& sceneName
) {

    if (
        sceneName.length() == 0 ||
        !sdReady
    ) {
        return "";
    }

    const String candidates[] = {
        "/nfc/scenes/" +
            sceneName +
            "_ok.png",
        "/nfc/scenes/" +
            sceneName +
            "_ok.PNG",
        "/scenes/" +
            sceneName +
            "_ok.png",
        "/scenes/" +
            sceneName +
            "_ok.PNG"
    };

    for (
        uint8_t i = 0;
        i < sizeof(candidates) / sizeof(candidates[0]);
        ++i
    ) {
        if (
            SD.exists(
                candidates[i]
            )
        ) {
            return candidates[i];
        }
    }

    return "";
}

bool playNfcSuccessAnimation() {

    if (
        !sdReady
    ) {
        return false;
    }

    const String folders[] = {
        "/nfc/success",
        "/scenes/success",
        "/success"
    };

    String folder =
        "";

    for (
        uint8_t i = 0;
        i < sizeof(folders) / sizeof(folders[0]);
        ++i
    ) {
        File dir =
            SD.open(
                folders[i]
            );

        if (
            dir &&
            dir.isDirectory()
        ) {
            folder =
                folders[i];
            dir.close();
            break;
        }

        if (
            dir
        ) {
            dir.close();
        }
    }

    if (
        folder.length() == 0
    ) {
        return false;
    }

    File dir =
        SD.open(
            folder
        );

    if (
        !dir ||
        !dir.isDirectory()
    ) {
        return false;
    }

    bool playedAny =
        false;
    bool composeToCanvas =
        captureDisplayToSceneCanvas() &&
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr;
    uint16_t frameIndex =
        0;

    for (
        uint16_t attempt = 0;
        attempt < 128;
        ++attempt
    ) {
        File file =
            dir.openNextFile();

        if (
            !file
        ) {
            break;
        }

        String filename =
            file.name();

        if (
            file.isDirectory() ||
            isHiddenSdEntry(
                filename
            ) ||
            (
                !filename.endsWith(
                    ".png"
                ) &&
                !filename.endsWith(
                    ".PNG"
                )
            )
        ) {
            file.close();
            continue;
        }

        String fullPath =
            filename.startsWith(
                "/"
            ) ?
            filename :
            folder +
                "/" +
                filename;

        file.close();

        bool shouldDrawFrame =
            (
                frameIndex %
                NFC_SUCCESS_FRAME_STEP
            ) == 0;

        ++frameIndex;

        if (
            !shouldDrawFrame
        ) {
            continue;
        }

        File pngFile =
            SD.open(
                fullPath
            );

        if (
            !pngFile
        ) {
            continue;
        }

        size_t size =
            pngFile.size();

        uint8_t* buf =
            (uint8_t*)malloc(
                size
            );

        if (
            !buf
        ) {
            pngFile.close();
            continue;
        }

        pngFile.read(
            buf,
            size
        );

        pngFile.close();

        uint32_t imageWidth;
        uint32_t imageHeight;

        readPngSize(
            buf,
            size,
            imageWidth,
            imageHeight
        );

        float scaleX =
            imageWidth > SCREEN_WIDTH ?
            (float)SCREEN_WIDTH /
                (float)imageWidth :
            1.0f;

        float scaleY =
            imageHeight > SCREEN_HEIGHT ?
            (float)SCREEN_HEIGHT /
                (float)imageHeight :
            1.0f;

        float scale =
            scaleX < scaleY ?
            scaleX :
            scaleY;

        if (
            scale <= 0.0f
        ) {
            scale =
                1.0f;
        }

        int32_t drawWidth =
            (int32_t)(
                (float)imageWidth *
                scale
            );

        int32_t drawHeight =
            (int32_t)(
                (float)imageHeight *
                scale
            );

        int32_t x =
            (
                SCREEN_WIDTH -
                drawWidth
            ) / 2;

        int32_t y =
            (
                SCREEN_HEIGHT -
                drawHeight
            ) / 2;

        bool drewFrame =
            false;

        if (
            composeToCanvas
        ) {
            sceneCanvas.pushSprite(
                &frameCanvas,
                0,
                0
            );

            drewFrame =
                frameCanvas.drawPng(
                    buf,
                    size,
                    x,
                    y,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    0,
                    0,
                    scale,
                    scale
                );

            if (
                drewFrame
            ) {
                frameCanvas.pushSprite(
                    0,
                    0
                );
            }
        } else {
            drewFrame =
                M5.Display.drawPng(
                    buf,
                    size,
                    x,
                    y,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    0,
                    0,
                    scale,
                    scale
                );
        }

        free(
            buf
        );

        if (
            drewFrame
        ) {
            playedAny =
                true;
            waitWithService(
                NFC_SUCCESS_FRAME_DELAY
            );
        }
    }

    dir.close();

    resetSceneCache();

    return playedAny;
}

bool handleNfcPayload(
    const String& payload
) {

    String personaName =
        extractNfcPersonaName(
            payload
        );

    if (
        personaName.length() > 0
    ) {
        Serial.print(
            "[nfc] payload: "
        );
        Serial.println(
            payload
        );

        selectPersona(
            personaName
        );

        return true;
    }

    String sceneName =
        extractNfcSceneName(
            payload
        );

    if (
        sceneName.length() == 0
    ) {
        return false;
    }

    String scenePath =
        resolveNfcScenePath(
            sceneName
        );

    String sceneOkPath =
        resolveNfcSceneOkPath(
            sceneName
        );

    if (
        scenePath.length() == 0
    ) {
        Serial.print(
            "[nfc] scene not found: "
        );
        Serial.println(
            sceneName
        );
        return false;
    }

    Serial.print(
        "[nfc] payload: "
    );
    Serial.println(
        payload
    );

    bool restoreFlashBase =
        captureDisplayToSceneCanvas() &&
        frameCanvasReady &&
        frameCanvas.getBuffer() !=
            nullptr;

    playNfcSuccessAnimation();

    if (
        restoreFlashBase
    ) {
        restoreCapturedSceneCanvas();
    } else {
        presentBlackFrame();
    }

    for (
        uint8_t i = 0;
        i < NFC_FLASH_REPEAT_COUNT;
        ++i
    ) {
        if (
            drawPngFileCentered(
                scenePath,
                !restoreFlashBase
            )
        ) {
            waitWithService(
                NFC_FLASH_ON_MS
            );
        }

        if (
            restoreFlashBase
        ) {
            restoreCapturedSceneCanvas();
        } else {
            presentBlackFrame();
        }

        waitWithService(
            NFC_FLASH_OFF_MS
        );
    }

    if (
        sceneOkPath.length() > 0 &&
        drawPngFileCentered(
            sceneOkPath,
            !restoreFlashBase
        )
    ) {
        waitWithService(
            NFC_SCENE_OK_HOLD_MS
        );
    }

    return true;
}

void nfcBegin() {

    for (
        uint8_t i = 0;
        i < MFRC522_I2C::MF_KEY_SIZE;
        ++i
    ) {
        nfcKey.keyByte[i] =
            0xFF;
    }

    auto pinNumSda =
        M5.getPin(
            m5::pin_name_t::port_a_sda
        );
    auto pinNumScl =
        M5.getPin(
            m5::pin_name_t::port_a_scl
        );

    Wire.begin(
        pinNumSda,
        pinNumScl,
        NFC_I2C_CLOCK
    );

    delay(20);

    nfcReaderVersion =
        nfcReader.PCD_ReadRegister(
            MFRC522_I2C::VersionReg
        );

    Serial.print(
        "[nfc] version=0x"
    );
    Serial.println(
        nfcReaderVersion,
        HEX
    );

    if (
        nfcReaderVersion == 0x00 ||
        nfcReaderVersion == 0xFF
    ) {
        Serial.println(
            "[nfc] reader not detected"
        );
        nfcReady =
            false;
        return;
    }

    nfcReader.PCD_Reset();
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::TModeReg,
        0x80
    );
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::TPrescalerReg,
        0xA9
    );
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::TReloadRegH,
        0x03
    );
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::TReloadRegL,
        0xE8
    );
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::TxASKReg,
        0x40
    );
    nfcReader.PCD_WriteRegister(
        MFRC522_I2C::ModeReg,
        0x3D
    );
    nfcReader.PCD_AntennaOn();

    nfcReady =
        true;

    Serial.println(
        "[nfc] ready"
    );
}

bool nfcLoop() {

    if (
        !nfcReady
    ) {
        return false;
    }

    unsigned long now =
        millis();

    if (
        now - lastNfcPollAt <
        NFC_POLL_INTERVAL
    ) {
        return false;
    }

    lastNfcPollAt =
        now;

    String uidHex =
        "";
    String payload =
        "";

    if (
        !pollNfcCard(
            uidHex,
            payload
        )
    ) {
        if (
            lastNfcUid.length() > 0 &&
            now - lastNfcSeenAt >
                NFC_CARD_RELEASE_MS
        ) {
            lastNfcUid =
                "";
        }

        return false;
    }

    lastNfcSeenAt =
        now;

    bool sameCard =
        uidHex ==
        lastNfcUid;

    if (
        !sameCard
    ) {
        lastNfcUid =
            uidHex;
    } else {
        return false;
    }

    if (
        payload.length() == 0
    ) {
        Serial.print(
            "[nfc] uid="
        );
        Serial.print(
            uidHex
        );
        Serial.println(
            " no scene payload"
        );
        lastNfcHandledAt =
            now;
        return false;
    }

    if (
        !handleNfcPayload(
            payload
        )
    ) {
        lastNfcHandledAt =
            now;
        return false;
    }

    lastNfcHandledAt =
        millis();
    lastNfcSeenAt =
        lastNfcHandledAt;

    return true;
}

bool shouldDrawDialogBox(
    const String& state
) {

    return isChatLikeDuelState(
            state
        ) ||
        state == "coffee";
}

bool isChatLikeDuelState(
    const String& state
) {

    return state == "chat" ||
        state == "angry" ||
        state == "happy" ||
        state == "sad";
}

bool personaDefaultsToLeft(
    const String& persona
) {

    return persona ==
        "zhang_zong";
}

bool isPenguinWitchPair() {

    return (
        duelLeftPersona ==
            "xiao_hong" &&
        duelRightPersona ==
            "xiao_xia"
    ) ||
        (
            duelLeftPersona ==
                "xiao_xia" &&
            duelRightPersona ==
                "xiao_hong"
        );
}

bool shouldDisplayLeftPersona(
    const String& persona
) {

    if (
        persona ==
        "xiao_hong" &&
        isPenguinWitchPair()
    ) {
        return true;
    }

    return personaDefaultsToLeft(
        persona
    );
}

bool shouldMirrorPersonaOnScreen(
    const String& persona
) {

    (void)persona;

    return false;
}

bool shouldUseLeftBubble(
    const String& speakerPersona
) {

    return shouldDisplayLeftPersona(
        speakerPersona
    );
}

const char* bubblePathForSpeaker(
    const String& speakerPersona
) {

    if (
        !duelMode &&
        currentState ==
            "coffee"
    ) {
        return BUBBLE_LEFT_PATH;
    }

    return shouldUseLeftBubble(
        speakerPersona
    ) ?
        BUBBLE_LEFT_PATH :
        BUBBLE_RIGHT_PATH;
}

void drawBubbleOverlay(
    lgfx::LGFXBase& target,
    const String& speakerPersona
) {

    if (
        !sdReady
    ) {
        return;
    }

    String path =
        bubblePathForSpeaker(
            speakerPersona
        );

    File bubbleFile =
        SD.open(
            path
        );

    if (
        !bubbleFile
    ) {
        return;
    }

    size_t size =
        bubbleFile.size();

    uint8_t* buf =
        (uint8_t*)malloc(
            size
        );

    if (
        !buf
    ) {
        bubbleFile.close();
        return;
    }

    bubbleFile.read(
        buf,
        size
    );

    bubbleFile.close();

    target.drawPng(
        buf,
        size,
        0,
        0,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0,
        0,
        1.0f,
        1.0f
    );

    free(
        buf
    );
}

uint16_t utf8CharLen(
    const String& text
) {

    uint16_t count =
        0;

    for (
        uint16_t i = 0;
        i < text.length();
        ++i
    ) {
        uint8_t c =
            (uint8_t)text[i];

        if (
            (c & 0xC0) !=
            0x80
        ) {
            ++count;
        }
    }

    return count;
}

uint16_t utf8ByteOffset(
    const String& text,
    uint16_t charIndex
) {

    uint16_t count =
        0;

    for (
        uint16_t i = 0;
        i < text.length();
        ++i
    ) {
        uint8_t c =
            (uint8_t)text[i];

        if (
            (c & 0xC0) !=
            0x80
        ) {
            if (
                count ==
                charIndex
            ) {
                return i;
            }

            ++count;
        }
    }

    return text.length();
}

String utf8Window(
    const String& text,
    uint16_t startChar,
    uint16_t maxChars
) {

    uint16_t startByte =
        utf8ByteOffset(
            text,
            startChar
        );

    uint16_t endByte =
        utf8ByteOffset(
            text,
            startChar + maxChars
        );

    return text.substring(
        startByte,
        endByte
    );
}

String scrollingTextWindow(
    const String& text,
    uint16_t maxChars
) {

    uint16_t charCount =
        utf8CharLen(
            text
        );

    if (
        charCount <=
        maxChars
    ) {
        lastScrollText =
            text;
        textScrollIndex =
            0;
        return text;
    }

    if (
        lastScrollText !=
        text
    ) {
        lastScrollText =
            text;
        textScrollIndex =
            0;
        lastTextScrollAt =
            millis();
    }

    if (
        millis() -
        lastTextScrollAt >
        350
    ) {
        lastTextScrollAt =
            millis();
        ++textScrollIndex;

        if (
            textScrollIndex >
            charCount
        ) {
            textScrollIndex =
                0;
        }
    }

    uint16_t maxStart =
        charCount -
        maxChars;

    uint16_t start =
        textScrollIndex;

    if (
        start >
        maxStart + 6
    ) {
        start =
            0;
    } else if (
        start >
        maxStart
    ) {
        start =
            maxStart;
    }

    return utf8Window(
        text,
        start,
        maxChars
    );
}

bool tryOpenAnimationFolder(
    const String& state
) {

    if (
        !sdReady
    ) {
        return false;
    }

    String folder =
        animationFolderPath(
            currentPersona,
            state
        );

    Serial.print(
        "Open folder: "
    );
    Serial.println(
        folder
    );

    File dir =
        SD.open(folder);

    if (
        !dir ||
        !dir.isDirectory()
    ) {

        Serial.print(
            "Folder open fail: "
        );
        Serial.println(
            folder
        );

        if (dir) {
            dir.close();
        }

        return false;
    }

    currentDir = dir;
    folderOpened = true;

    File probeDir =
        SD.open(folder);

    if (
        probeDir &&
        probeDir.isDirectory()
    ) {
        for (
            uint8_t i = 0;
            i < 8;
            ++i
        ) {
            File entry =
                probeDir.openNextFile();

            if (
                !entry
            ) {
                break;
            }

            Serial.print(
                "  entry: "
            );
            Serial.print(
                entry.name()
            );
            Serial.print(
                " dir="
            );
            Serial.print(
                entry.isDirectory() ?
                "yes" :
                "no"
            );
            Serial.print(
                " size="
            );
            Serial.println(
                entry.size()
            );
            entry.close();
        }
    }

    if (
        probeDir
    ) {
        probeDir.close();
    }

    return true;
}

void resetAnimationFolder() {

    if (
        folderOpened
    ) {
        currentDir.close();
    }

    folderOpened =
        false;

    animationLooped =
        false;
}

void resetDuelFolders() {

    if (
        duelLeftOpened
    ) {
        duelLeftDir.close();
    }

    if (
        duelRightOpened
    ) {
        duelRightDir.close();
    }

    duelLeftOpened =
        false;

    duelRightOpened =
        false;

    duelLeftDone =
        false;

    duelRightDone =
        false;
}

void resetTransferFolder() {

    if (
        transferOpened
    ) {
        transferDir.close();
    }

    transferOpened =
        false;

    transferDone =
        false;

    transferHoldActive =
        false;

    transferHoldStartedAt =
        0;

    transferStageState =
        "";

    transferStageNextState =
        "";
}

void resetRenderedScene() {

    renderedSceneKey =
        "";
}

void resetSceneCache() {

    renderedSceneKey =
        "";

    cachedSceneKey =
        "";
}

const char* resetReasonName(
    esp_reset_reason_t reason
) {

    switch (
        reason
    ) {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXT";
        case ESP_RST_SW:
            return "SW";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "UNKNOWN";
    }
}

void logHeap(
    const char* label
) {

    Serial.print(
        "[heap] "
    );
    Serial.print(
        label
    );
    Serial.print(
        " free="
    );
    Serial.print(
        ESP.getFreeHeap()
    );
    Serial.print(
        " min="
    );
    Serial.print(
        ESP.getMinFreeHeap()
    );
    Serial.print(
        " maxAlloc="
    );
    Serial.print(
        ESP.getMaxAllocHeap()
    );

    if (
        psramFound()
    ) {
        Serial.print(
            " psramFree="
        );
        Serial.print(
            ESP.getFreePsram()
        );
    }

    Serial.println();
}

bool initSDCard() {

    const uint32_t speeds[] = {
        25000000,
        10000000,
        4000000,
        1000000
    };

    pinMode(
        SD_SPI_CS_PIN,
        OUTPUT
    );

    digitalWrite(
        SD_SPI_CS_PIN,
        HIGH
    );

    SPI.begin(
        SD_SPI_SCK_PIN,
        SD_SPI_MISO_PIN,
        SD_SPI_MOSI_PIN,
        SD_SPI_CS_PIN
    );

    delay(500);

    for (
        uint8_t i = 0;
        i < sizeof(speeds) / sizeof(speeds[0]);
        ++i
    ) {

        SD.end();
        delay(150);

        Serial.print(
            "SD begin @ "
        );
        Serial.println(
            speeds[i]
        );

        if (
            SD.begin(
                SD_SPI_CS_PIN,
                SPI,
                speeds[i]
            )
        ) {

            uint8_t cardType =
                SD.cardType();

            if (
                cardType != CARD_NONE
            ) {
                Serial.println(
                    "SD mounted"
                );
                return true;
            }

            Serial.println(
                "SD card none"
            );
        }
    }

    return false;
}

bool loadDisplayUiFont() {

    const char* fontPaths[] = {
        UI_FONT_PRIMARY_PATH,
        UI_FONT_FALLBACK_PATH
    };

    for (
        uint8_t i = 0;
        i < sizeof(fontPaths) / sizeof(fontPaths[0]);
        ++i
    ) {

        Serial.print(
            "[font] load "
        );
        Serial.print(
            "display"
        );
        Serial.print(
            " <- "
        );
        Serial.println(
            fontPaths[i]
        );

        if (
            M5.Display.loadFont(
                SD,
                fontPaths[i]
            )
        ) {
            activeUiFontPath =
                fontPaths[i];

            Serial.print(
                "[font] loaded "
            );
            Serial.print(
                "display"
            );
            Serial.print(
                " <- "
            );
            Serial.println(
                fontPaths[i]
            );

            return true;
        }
    }

    Serial.print(
        "[font] load fail: "
    );
    Serial.println(
        "display"
    );

    return false;
}

bool loadFrameUiFont() {

    const char* fontPaths[] = {
        UI_FONT_PRIMARY_PATH,
        UI_FONT_FALLBACK_PATH
    };

    for (
        uint8_t i = 0;
        i < sizeof(fontPaths) / sizeof(fontPaths[0]);
        ++i
    ) {

        Serial.print(
            "[font] load "
        );
        Serial.print(
            "frame"
        );
        Serial.print(
            " <- "
        );
        Serial.println(
            fontPaths[i]
        );

        if (
            frameCanvas.loadFont(
                SD,
                fontPaths[i]
            )
        ) {
            activeUiFontPath =
                fontPaths[i];

            Serial.print(
                "[font] loaded "
            );
            Serial.print(
                "frame"
            );
            Serial.print(
                " <- "
            );
            Serial.println(
                fontPaths[i]
            );

            return true;
        }
    }

    Serial.print(
        "[font] load fail: "
    );
    Serial.println(
        "frame"
    );

    return false;
}

void applyUiFont(
    lgfx::LGFXBase& target,
    bool fontLoaded
) {

    if (
        !fontLoaded
    ) {
        target.setFont(
            &fonts::efontCN_16
        );
    }
}

void drawCoffeeTextLine() {

    String text =
        displayText;

    if (
        lastCoffeeRenderedText ==
        text
    ) {
        return;
    }

    lastCoffeeRenderedText =
        text;

    drawBubbleOverlay(
        (lgfx::LGFXBase&)M5.Display,
        currentPersona
    );

    applyUiFont(
        (lgfx::LGFXBase&)M5.Display,
        displayUiFontLoaded
    );

    M5.Display.setTextSize(
        CHAT_TEXT_SIZE
    );

    M5.Display.setTextColor(
        BLACK
    );
    M5.Display.setTextWrap(
        false
    );

    int32_t x =
        COFFEE_TEXT_X;

    M5.Display.setClipRect(
        COFFEE_TEXT_X,
        COFFEE_TEXT_Y,
        COFFEE_TEXT_W,
        COFFEE_TEXT_H
    );

    M5.Display.setCursor(
        x,
        COFFEE_TEXT_Y
    );

    M5.Display.print(
        text
    );

    M5.Display.clearClipRect();

    M5.Display.setTextWrap(
        true
    );
    M5.Display.setTextSize(
        1.0f
    );
}

// ======================
// 文本框
// ======================
void drawTextBox() {

    if (
        currentState ==
        "coffee"
    ) {
        drawCoffeeTextLine();
        return;
    }

    String text =
        scrollingTextWindow(
            displayText,
            DUEL_TEXT_WINDOW_CHARS
        );

    text.trim();

    if (
        text.length() == 0
    ) {
        return;
    }

    drawBubbleOverlay(
        (lgfx::LGFXBase&)M5.Display,
        currentPersona
    );

    applyUiFont(
        (lgfx::LGFXBase&)M5.Display,
        displayUiFontLoaded
    );

    M5.Display.setTextSize(
        CHAT_TEXT_SIZE
    );

    M5.Display.setTextColor(
        BLACK
    );

    M5.Display.setCursor(
        DIALOG_TEXT_X,
        DIALOG_TEXT_Y
    );

    M5.Display.print(text);

    M5.Display.setTextSize(
        1.0f
    );
}

void drawDuelTextBox() {

    lgfx::LGFXBase& target =
        drawingToFrameCanvas ?
        (lgfx::LGFXBase&)frameCanvas :
        (lgfx::LGFXBase&)M5.Display;
    bool targetFontLoaded =
        drawingToFrameCanvas ?
        frameUiFontLoaded :
        displayUiFontLoaded;

    String text =
        scrollingTextWindow(
            duelText,
            DUEL_TEXT_WINDOW_CHARS
        );

    text.trim();

    if (
        text.length() == 0
    ) {
        return;
    }

    drawBubbleOverlay(
        target,
        duelSpeakerPersona
    );

    applyUiFont(
        target,
        targetFontLoaded
    );

    target.setTextSize(
        CHAT_TEXT_SIZE
    );

    target.setTextColor(
        BLACK
    );

    target.setCursor(
        DIALOG_TEXT_X,
        DIALOG_TEXT_Y
    );

    target.print(text);

    target.setTextSize(
        1.0f
    );

}

String personaDisplayName(
    const String& persona
) {

    if (
        persona ==
        "xiao_hong"
    ) {
        return "小红";
    }

    if (
        persona ==
        "zhang_zong"
    ) {
        return "张总";
    }

    if (
        persona ==
        "xiao_xia"
    ) {
        return "小夏";
    }

    return persona;
}

String transferPeerPersona() {

    if (
        currentPersona ==
        duelLeftPersona
    ) {
        return duelRightPersona;
    }

    if (
        currentPersona ==
        duelRightPersona
    ) {
        return duelLeftPersona;
    }

    return duelRightPersona;
}

String stageDisplayText(
    const String& state,
    const String& leftPersona,
    const String& rightPersona,
    const String& fallbackText
) {

    if (
        state == "outdoor"
    ) {
        return personaDisplayName(
            leftPersona
        ) +
        "遇见" +
        personaDisplayName(
            rightPersona
        );
    }

    if (
        state == "leave"
    ) {
        return String(
            "谈话结束，"
        ) +
        personaDisplayName(
            leftPersona
        ) +
        "准备回家。";
    }

    return fallbackText;
}

void drawCenteredTransferText() {

    String text =
        duelText;

    applyUiFont(
        (lgfx::LGFXBase&)M5.Display,
        displayUiFontLoaded
    );

    M5.Display.setTextSize(
        TRANSFER_TEXT_SIZE
    );

    int32_t textWidth =
        M5.Display.textWidth(
            text
        );

    int32_t textX =
        (
            SCREEN_WIDTH -
            textWidth
        ) / 2;

    if (
        textX < 4
    ) {
        textX =
            4;
    }

    int32_t textY =
        (
            SCREEN_HEIGHT -
            M5.Display.fontHeight()
        ) / 2;

    M5.Display.setTextColor(
        BLACK
    );
    M5.Display.setCursor(
        textX,
        textY
    );

    M5.Display.print(
        text
    );

    M5.Display.setTextSize(
        1.0f
    );
}

void reportPersona() {

    Serial.print(
        "PERSONA|"
    );

    Serial.println(
        currentPersona
    );

    Serial.flush();

    if (
        wsConnected
    ) {
        wsClient.send(
            String(
                "PERSONA|"
            ) +
            currentPersona
        );
    }

    lastPersonaReportAt =
        millis();
}

const char* chargingStateName(
    int state
) {

    switch (
        state
    ) {
        case 0:
            return "discharging";
        case 1:
            return "charging";
        case 2:
            return "unknown";
        default:
            return "unsupported";
    }
}

void reportPowerStatus() {

    int32_t batteryLevel =
        M5.Power.getBatteryLevel();

    int16_t batteryVoltage =
        M5.Power.getBatteryVoltage();

    int32_t batteryCurrent =
        M5.Power.getBatteryCurrent();

    int16_t vbusVoltage =
        M5.Power.getVBUSVoltage();

    int chargingState =
        (int)M5.Power.isCharging();

    Serial.print(
        "[power] battery="
    );

    if (
        batteryLevel >= 0
    ) {
        Serial.print(
            batteryLevel
        );
        Serial.print(
            "%"
        );
    } else {
        Serial.print(
            "unsupported"
        );
    }

    Serial.print(
        " bat="
    );
    Serial.print(
        batteryVoltage
    );
    Serial.print(
        "mV current="
    );
    Serial.print(
        batteryCurrent
    );
    Serial.print(
        "mA vbus="
    );
    Serial.print(
        vbusVoltage
    );
    Serial.print(
        "mV charge="
    );
    Serial.println(
        chargingStateName(
            chargingState
        )
    );

    lastPowerReportAt =
        millis();
}

void selectPersona(
    const String& persona
) {

    bool changed =
        currentPersona !=
        persona;

    if (
        changed
    ) {
        currentPersona =
            persona;

        personaPrefs.putString(
            "persona",
            currentPersona
        );
    }

    if (
        currentState !=
        "idle"
    ) {
        currentState =
            "idle";
    }

    if (
        !changed &&
        currentPersona ==
        persona
    ) {
        Serial.println(
            "Reload persona scene"
        );
    }

    duelMode =
        false;

    clearGiftFlags();

    resetDuelFolders();
    resetTransferFolder();

    displayText =
        "Persona: " +
        currentPersona;

    resetAnimationFolder();

    resetRenderedScene();

    playAnimationFrame();
    reportPersona();
    updateBleAdvertising();
}

void receivePersonaCommand() {

    String msg =
        Serial.readStringUntil(
            '\n'
        );

    msg.trim();

    Serial.println(
        msg
    );

    String persona =
        "";

    if (
        msg.startsWith(
            "PERSONA|"
        )
    ) {
        persona =
            msg.substring(
                8
            );
    } else if (
        msg.startsWith(
            "SEL|"
        )
    ) {
        persona =
            msg.substring(
                4
            );
    }

    persona.trim();

    if (
        persona.length() > 0
    ) {
        selectPersona(
            persona
        );
    }
}

void startDuelScene(
    const String& leftPersona,
    const String& rightPersona,
    const String& speakerPersona,
    const String& state,
    const String& text,
    bool oneShot,
    const String& nextState
) {

    String previousDuelState =
        duelState;

    duelLeftPersona =
        leftPersona;

    duelRightPersona =
        rightPersona;

    duelSpeakerPersona =
        speakerPersona;

    duelState =
        state;

    duelText =
        stageDisplayText(
            state,
            leftPersona,
            rightPersona,
            text
        );

    duelOneShot =
        oneShot;

    duelNextState =
        nextState;

    duelMode =
        true;

    resetAnimationFolder();
    resetDuelFolders();
    resetTransferFolder();

    if (
        isTransferStage(
            state
        )
    ) {
        transferBackgroundState =
            previousDuelState.length() > 0 ?
            previousDuelState :
            currentState;
    }

    if (
        state == "leave" &&
        nextState == "idle" &&
        isChatLikeDuelState(
            previousDuelState
        )
    ) {
        overlayCapturedBaseReady =
            cleanFrameCaptureReady ||
            captureDisplayToSceneCanvas();

        giftPending =
            true;

        giftStartsTransfer =
            true;

        duelMode =
            false;

        duelOneShot =
            false;

        resetAnimationFolder();
        resetDuelFolders();
        resetTransferFolder();
        resetRenderedScene();

        Serial.println(
            "Queue gift before transfer"
        );

        return;
    }

    resetRenderedScene();

    Serial.print(
        "Duel state: "
    );
    Serial.println(
        duelState
    );

    playDuelFrame();
}

void receiveDuelCommand() {

    String msg =
        Serial.readStringUntil(
            '\n'
        );

    msg.trim();

    Serial.println(
        msg
    );

    handleDuelMessage(
        msg
    );
}

void handleDuelMessage(
    String msg
) {

    msg.trim();

    if (
        msg.startsWith(
            "DUELSTAGE|"
        )
    ) {

        int p1 =
            msg.indexOf(
                '|'
            );
        int p2 =
            msg.indexOf(
                '|',
                p1 + 1
            );
        int p3 =
            msg.indexOf(
                '|',
                p2 + 1
            );
        int p4 =
            msg.indexOf(
                '|',
                p3 + 1
            );
        int p5 =
            msg.indexOf(
                '|',
                p4 + 1
            );

        if (
            p1 < 0 ||
            p2 < 0 ||
            p3 < 0 ||
            p4 < 0 ||
            p5 < 0
        ) {
            Serial.println(
                "DUELSTAGE parse fail"
            );
            return;
        }

        String state =
            msg.substring(
                p1 + 1,
                p2
            );

        String nextState =
            msg.substring(
                p2 + 1,
                p3
            );

        String leftPersona =
            msg.substring(
                p3 + 1,
                p4
            );

        String rightPersona =
            msg.substring(
                p4 + 1,
                p5
            );

        String text =
            msg.substring(
                p5 + 1
            );

        startDuelScene(
            leftPersona,
            rightPersona,
            leftPersona,
            state,
            text,
            true,
            nextState
        );

        return;
    }

    int p1 =
        msg.indexOf(
            '|'
        );

    int p2 =
        msg.indexOf(
            '|',
            p1 + 1
        );

    int p3 =
        msg.indexOf(
            '|',
            p2 + 1
        );

    int p4 =
        msg.indexOf(
            '|',
            p3 + 1
        );

    int p5 =
        msg.indexOf(
            '|',
            p4 + 1
        );

    if (
        p1 < 0 ||
        p2 < 0 ||
        p3 < 0 ||
        p4 < 0 ||
        p5 < 0
    ) {
        Serial.println(
            "DUEL parse fail"
        );
        return;
    }

    startDuelScene(
        msg.substring(
            p1 + 1,
            p2
        ),
        msg.substring(
            p2 + 1,
            p3
        ),
        msg.substring(
            p3 + 1,
            p4
        ),
        msg.substring(
            p4 + 1,
            p5
        ),
        msg.substring(
            p5 + 1
        ),
        false,
        ""
    );
}

void processCommandLine(
    String msg
) {

    msg.trim();

    if (
        !msg.length()
    ) {
        return;
    }

    if (
        msg.startsWith(
            "PCMWS"
        )
    ) {
        handleWsPcmMessage(
            msg
        );

        return;
    }

    if (
        msg.startsWith(
            "PERSONA|"
        ) ||
        msg.startsWith(
            "SEL|"
        )
    ) {
        String persona =
            msg.startsWith(
                "PERSONA|"
            ) ?
            msg.substring(
                8
            ) :
            msg.substring(
                4
            );

        persona.trim();

        if (
            persona.length() > 0
        ) {
            selectPersona(
                persona
            );
        }

        return;
    }

    if (
        msg.startsWith(
            "DUEL"
        )
    ) {
        handleDuelMessage(
            msg
        );

        return;
    }

    if (
        msg.startsWith(
            "TXT|"
        )
    ) {
        int p1 =
            msg.indexOf(
                '|',
                4
            );

        if (
            p1 > 0
        ) {
            currentState =
                msg.substring(
                    4,
                    p1
                );

            displayText =
                msg.substring(
                    p1 + 1
                );

            if (
                currentState ==
                "coffee"
            ) {
                lastCoffeeRenderedText =
                    "";
            }

            duelMode =
                false;

            resetDuelFolders();

            Serial.print(
                "State: "
            );

            Serial.println(
                currentState
            );

            Serial.print(
                "Text: "
            );

            Serial.println(
                displayText
            );

            resetAnimationFolder();

            resetRenderedScene();

            enableAnimation =
                true;

            clearGiftFlags();

            playAnimationFrame();
        }

        return;
    }

    Serial.print(
        "CMD ignored: "
    );
    Serial.println(
        msg
    );
}

void onWsMessage(
    websockets::WebsocketsMessage message
) {

    processCommandLine(
        message.data()
    );
}

void onWsEvent(
    websockets::WebsocketsEvent event,
    String data
) {

    if (
        event ==
        websockets::WebsocketsEvent::ConnectionOpened
    ) {
        wsConnected =
            true;

        Serial.println(
            "[ws] connected"
        );

        wsClient.send(
            String(
                "HELLO|"
            ) +
            currentPersona
        );
    } else if (
        event ==
        websockets::WebsocketsEvent::ConnectionClosed
    ) {
        wsConnected =
            false;

        Serial.println(
            "[ws] closed"
        );
    }
}

void beginWifiStation(
    bool resetRadio,
    const char* reason
) {

    if (
        resetRadio
    ) {
        WiFi.disconnect(
            true,
            true
        );
        delay(120);
        WiFi.mode(
            WIFI_STA
        );
        WiFi.setSleep(
            false
        );
        WiFi.persistent(
            false
        );
    }

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );

    lastWifiBeginAt =
        millis();

    Serial.print(
        "[ws] "
    );
    Serial.print(
        reason
    );
    Serial.print(
        ": "
    );
    Serial.println(
        WIFI_SSID
    );
}

void logWifiScanSummary() {

    int count =
        WiFi.scanNetworks(
            false,
            true
        );

    if (
        count <= 0
    ) {
        Serial.println(
            "[ws] scan found no networks"
        );
        WiFi.scanDelete();
        return;
    }

    bool foundTarget =
        false;

    for (
        int i = 0;
        i < count;
        ++i
    ) {
        String ssid =
            WiFi.SSID(i);

        if (
            ssid !=
            WIFI_SSID
        ) {
            continue;
        }

        foundTarget =
            true;

        Serial.print(
            "[ws] scan target "
        );
        Serial.print(
            ssid
        );
        Serial.print(
            " rssi="
        );
        Serial.print(
            WiFi.RSSI(i)
        );
        Serial.print(
            " ch="
        );
        Serial.print(
            WiFi.channel(i)
        );
        Serial.print(
            " enc="
        );
        Serial.println(
            (int)WiFi.encryptionType(i)
        );
    }

    if (
        !foundTarget
    ) {
        Serial.print(
            "[ws] target ssid not found in scan: "
        );
        Serial.println(
            WIFI_SSID
        );
    }

    WiFi.scanDelete();
}

void onWiFiEvent(
    WiFiEvent_t event,
    WiFiEventInfo_t info
) {

    switch (
        event
    ) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println(
                "[ws] wifi sta start"
            );
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println(
                "[ws] wifi ap connected"
            );
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print(
                "[ws] wifi got ip="
            );
            Serial.println(
                WiFi.localIP()
            );
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            wsConnected =
                false;
            wifiConnectedReported =
                false;
            Serial.print(
                "[ws] wifi disconnected reason="
            );
            Serial.println(
                (int)info.wifi_sta_disconnected.reason
            );
            break;
        default:
            break;
    }
}

void wsBegin() {

    if (
        strlen(
            WIFI_SSID
        ) == 0
    ) {
        wsEnabled =
            false;

        Serial.println(
            "[ws] disabled"
        );

        return;
    }

    wsEnabled =
        true;

    WiFi.onEvent(
        onWiFiEvent
    );

    WiFi.mode(
        WIFI_STA
    );

    WiFi.setSleep(
        false
    );

    WiFi.persistent(
        false
    );

    wsClient.onMessage(
        onWsMessage
    );

    wsClient.onEvent(
        onWsEvent
    );

    Serial.print(
        "[ws] host="
    );
    Serial.println(
        WS_HOST
    );

    beginWifiStation(
        true,
        "wifi connecting"
    );
}

const char* wifiStatusName(
    wl_status_t status
) {

    switch (
        status
    ) {
        case WL_CONNECTED:
            return "connected";
        case WL_NO_SSID_AVAIL:
            return "no ssid";
        case WL_CONNECT_FAILED:
            return "connect failed";
        case WL_CONNECTION_LOST:
            return "connection lost";
        case WL_DISCONNECTED:
            return "disconnected";
        case WL_IDLE_STATUS:
            return "idle";
        default:
            return "unknown";
    }
}

void wsLoop() {

    if (
        !wsEnabled
    ) {
        return;
    }

    if (
        wsConnected
    ) {
        wsClient.poll();
        return;
    }

    unsigned long now =
        millis();

    if (
        now -
        lastWsAttemptAt <
        5000
    ) {
        return;
    }

    lastWsAttemptAt =
        now;

    wl_status_t wifiStatus =
        WiFi.status();

    if (
        wifiStatus !=
        WL_CONNECTED
    ) {
        wifiConnectedReported =
            false;

        Serial.print(
            "[ws] wifi status="
        );
        Serial.println(
            wifiStatusName(
                wifiStatus
            )
        );

        bool shouldRestartWifi =
            now -
            lastWifiBeginAt >=
            20000 ||
            wifiStatus ==
            WL_CONNECT_FAILED ||
            wifiStatus ==
            WL_CONNECTION_LOST ||
            wifiStatus ==
            WL_NO_SSID_AVAIL;

        if (
            shouldRestartWifi
        ) {
            WiFi.disconnect(
                false,
                true
            );
            delay(80);

            if (
                now -
                lastWifiScanAt >=
                30000
            ) {
                lastWifiScanAt =
                    now;
                logWifiScanSummary();
            }

            beginWifiStation(
                true,
                "retry wifi"
            );
        } else {
            Serial.print(
                "[ws] wait wifi: "
            );
            Serial.println(
                WIFI_SSID
            );
        }
        return;
    }

    if (
        !wifiConnectedReported
    ) {
        Serial.print(
            "[ws] wifi connected ip="
        );
        Serial.println(
            WiFi.localIP()
        );

        wifiConnectedReported =
            true;
    }

    String url =
        String(
            "ws://"
        ) +
        WS_HOST +
        ":" +
        String(
            WS_PORT
        ) +
        WS_PATH;

    Serial.print(
        "[ws] connect "
    );
    Serial.println(
        url
    );

    if (
        wsClient.connect(
            url
        )
    ) {
        wsConnected =
            true;

        Serial.println(
            "[ws] connect ok"
        );
    } else {
        Serial.println(
            "[ws] connect failed"
        );
    }
}

#if ENABLE_BLE_ENCOUNTER
uint8_t personaBleId(
    const String& persona
) {

    if (
        persona == "xiao_hong"
    ) {
        return 1;
    }

    if (
        persona == "zhang_zong"
    ) {
        return 2;
    }

    return 0;
}

String personaFromBleId(
    uint8_t id
) {

    if (
        id == 1
    ) {
        return "xiao_hong";
    }

    if (
        id == 2
    ) {
        return "zhang_zong";
    }

    return "";
}

uint16_t localBleDeviceId() {

    uint64_t mac =
        ESP.getEfuseMac();

    return (uint16_t)(
        mac ^
        (
            mac >> 16
        ) ^
        (
            mac >> 32
        )
    );
}

String bleNameForPersona() {

    return String(
        peerNamePrefix
    ) +
    currentPersona;
}

String buildProjectManufacturerData() {

    char payload[11];

    uint16_t deviceId =
        localBleDeviceId();

    payload[0] =
        (char)(
            projectCompanyId &
            0xFF
        );

    payload[1] =
        (char)(
            (
                projectCompanyId >>
                8
            ) &
            0xFF
        );

    payload[2] =
        'R';

    payload[3] =
        'N';

    payload[4] =
        projectProtocolVersion;

    payload[5] =
        (char)(
            deviceId &
            0xFF
        );

    payload[6] =
        (char)(
            (
                deviceId >>
                8
            ) &
            0xFF
        );

    payload[7] =
        personaBleId(
            currentPersona
        );

    payload[8] =
        3;

    payload[9] =
        payload[7];

    payload[10] =
        0;

    return String(
        payload,
        sizeof(payload)
    );
}

String parseBlePersonaFromName(
    const String& name
) {

    if (
        !name.startsWith(
            peerNamePrefix
        )
    ) {
        return "";
    }

    String persona =
        name.substring(
            strlen(
                peerNamePrefix
            )
        );

    persona.trim();

    return persona;
}

bool parseProjectManufacturerData(
    const String& data,
    String& persona
) {

    if (
        data.length() <
        11
    ) {
        return false;
    }

    const uint8_t* bytes =
        (const uint8_t*)data.c_str();

    uint16_t companyId =
        bytes[0] |
        (
            bytes[1] <<
            8
        );

    if (
        companyId !=
        projectCompanyId
    ) {
        return false;
    }

    if (
        bytes[2] != 'R' ||
        bytes[3] != 'N' ||
        bytes[4] != projectProtocolVersion
    ) {
        return false;
    }

    persona =
        personaFromBleId(
            bytes[7]
        );

    return persona.length() > 0;
}

bool readProjectBlePersona(
    BLEAdvertisedDevice& device,
    String& persona
) {

    if (
        device.haveManufacturerData()
    ) {
        String manufacturerData =
            device.getManufacturerData();

        if (
            parseProjectManufacturerData(
                manufacturerData,
                persona
            )
        ) {
            return true;
        }
    }

    if (
        device.haveName()
    ) {
        persona =
            parseBlePersonaFromName(
                device.getName().c_str()
            );

        if (
            persona.length() > 0
        ) {
            return true;
        }
    }

    return false;
}

void rememberBlePeer(
    const String& persona,
    const String& name,
    int rssi
) {

    if (
        rssi <
        minBleRssi
    ) {
        return;
    }

    if (
        persona.length() == 0 ||
        persona == currentPersona
    ) {
        return;
    }

    unsigned long now =
        millis();

    if (
        persona == bestPeerPersona
    ) {
        bestPeerSeenCount++;
    } else {
        bestPeerPersona =
            persona;

        bestPeerSeenCount =
            1;
    }

    bestPeerName =
        name;

    bestPeerRssi =
        rssi;

    bestPeerLastSeenAt =
        now;
}

class RednoteScanCallbacks : public BLEAdvertisedDeviceCallbacks {

    void onResult(
        BLEAdvertisedDevice device
    ) override {

        String persona =
            "";

        bool hasProjectService =
            device.isAdvertisingService(
                BLEUUID(
                    peerServiceUuid
                )
            );

        bool hasProjectPersona =
            readProjectBlePersona(
                device,
                persona
            );

        if (
            !hasProjectService &&
            !hasProjectPersona
        ) {
            return;
        }

        String name =
            device.haveName() ?
            String(
                device.getName().c_str()
            ) :
            "";

        rememberBlePeer(
            persona,
            name,
            device.getRSSI()
        );
    }
};

void updateBleAdvertising() {

    if (
        !bleAdvertiser
    ) {
        return;
    }

    localBleName =
        bleNameForPersona();

    BLEDevice::stopAdvertising();

    BLEAdvertisementData advertisementData;
    advertisementData.setFlags(
        0x06
    );
    advertisementData.setCompleteServices(
        BLEUUID(
            peerServiceUuid
        )
    );
    advertisementData.setManufacturerData(
        buildProjectManufacturerData()
    );

    BLEAdvertisementData scanResponseData;
    scanResponseData.setName(
        localBleName.c_str()
    );

    bleAdvertiser->setAdvertisementData(
        advertisementData
    );
    bleAdvertiser->setScanResponseData(
        scanResponseData
    );
    BLEDevice::startAdvertising();

    lastBleAdvertiseAt =
        millis();

    Serial.print(
        "[ble] advertising "
    );
    Serial.println(
        localBleName
    );
}

void bleBegin() {

    localBleName =
        bleNameForPersona();

    BLEDevice::init(
        localBleName.c_str()
    );

    bleAdvertiser =
        BLEDevice::getAdvertising();

    bleAdvertiser->setMinPreferred(
        0x06
    );

    bleAdvertiser->setMinPreferred(
        0x12
    );

    updateBleAdvertising();

    bleScanner =
        BLEDevice::getScan();

    bleScanner->setAdvertisedDeviceCallbacks(
        new RednoteScanCallbacks(),
        true
    );

    bleScanner->setActiveScan(
        true
    );

    bleScanner->setInterval(
        100
    );

    bleScanner->setWindow(
        80
    );

    bleReady =
        true;

    Serial.println(
        "[ble] ready"
    );
}

void reportEncounterIfNeeded() {

    if (
        !wsConnected ||
        bestPeerPersona.length() == 0
    ) {
        return;
    }

    unsigned long now =
        millis();

    if (
        now -
        bestPeerLastSeenAt >
        blePeerFreshMs
    ) {
        return;
    }

    if (
        bestPeerRssi <
        encounterRssiThreshold ||
        bestPeerSeenCount <
        2
    ) {
        return;
    }

    String pairKey =
        currentPersona <
        bestPeerPersona ?
        currentPersona + "|" + bestPeerPersona :
        bestPeerPersona + "|" + currentPersona;

    if (
        pairKey ==
        lastEncounterPairKey &&
        now -
        lastEncounterReportAt <
        encounterReportCooldownMs
    ) {
        return;
    }

    String msg =
        String(
            "ENCOUNTER|"
        ) +
        currentPersona +
        "|" +
        bestPeerPersona +
        "|" +
        String(
            bestPeerRssi
        );

    wsClient.send(
        msg
    );

    Serial.print(
        "[ble] encounter report: "
    );
    Serial.println(
        msg
    );

    lastEncounterPairKey =
        pairKey;

    lastEncounterReportAt =
        now;
}

void bleLoop() {

    if (
        !bleReady ||
        !bleScanner ||
        isPlayingAudio
    ) {
        return;
    }

    unsigned long now =
        millis();

    if (
        bleAdvertiser &&
        now -
        lastBleAdvertiseAt >
        10000
    ) {
        updateBleAdvertising();
    }

    if (
        now -
        lastBleScanAt <
        bleScanIntervalMs
    ) {
        reportEncounterIfNeeded();
        return;
    }

    lastBleScanAt =
        now;

    BLEScanResults* results =
        bleScanner->start(
            1,
            false
        );

    if (
        results != nullptr
    ) {
        bleScanner->clearResults();
    }

    reportEncounterIfNeeded();
}
#else
void updateBleAdvertising() {
}

void bleBegin() {
    Serial.println(
        "[ble] disabled"
    );
}

void bleLoop() {
}
#endif

// ======================
// 打开动画目录
// ======================
void openAnimationFolder() {

    if (folderOpened) {
        currentDir.close();
    }
    folderOpened = false;

    if (
        tryOpenAnimationFolder(
            currentState
        )
    ) {
        return;
    }

    if (
        currentState ==
        "coffee"
    ) {
        Serial.println(
            "Fallback coffee animation: idle"
        );

        String previousState =
            currentState;

        if (
            tryOpenAnimationFolder(
                "idle"
            )
        ) {
            currentState =
                previousState;
            return;
        }

        currentState =
            previousState;
    }

    if (
        currentState !=
        "idle"
    ) {

        Serial.println(
            "Fallback state: idle"
        );

        currentState =
            "idle";

        tryOpenAnimationFolder(
            currentState
        );
    }
}

bool openDuelFolder(
    File& dir,
    bool& opened,
    const String& persona,
    const String& state
) {

    if (
        !sdReady
    ) {
        return false;
    }

    if (
        opened
    ) {
        dir.close();
    }

    opened =
        false;

    String folder =
        animationFolderPath(
            persona,
            state
        );

    Serial.print(
        "Open duel folder: "
    );
    Serial.println(
        folder
    );

    File next =
        SD.open(folder);

    if (
        !next ||
        !next.isDirectory()
    ) {

        if (next) {
            next.close();
        }

        if (
            state !=
            "idle"
        ) {
            String fallback =
                animationFolderPath(
                    persona,
                    "idle"
                );

            Serial.print(
                "Open duel fallback: "
            );
            Serial.println(
                fallback
            );

            next =
                SD.open(fallback);
        }
    }

    if (
        !next ||
        !next.isDirectory()
    ) {
        Serial.print(
            "Duel folder fail: "
        );
        Serial.println(
            persona
        );

        if (next) {
            next.close();
        }

        return false;
    }

    dir =
        next;
    opened =
        true;

    return true;
}

bool isTransferStage(
    const String& state
) {

    return state == "outdoor" ||
        state == "leave";
}

bool openTransferFolder() {

    if (
        !sdReady
    ) {
        return false;
    }

    const String folders[] = {
        animationFolderPath(
            currentPersona,
            "transfer"
        ),
        "/transfer",
        "/animations/transfer"
    };

    for (
        uint8_t i = 0;
        i < sizeof(folders) / sizeof(folders[0]);
        ++i
    ) {
        Serial.print(
            "Open transfer folder: "
        );
        Serial.println(
            folders[i]
        );

        File dir =
            SD.open(
                folders[i]
            );

        if (
            dir &&
            dir.isDirectory()
        ) {
            transferDir =
                dir;

            transferOpened =
                true;

            return true;
        }

        if (
            dir
        ) {
            dir.close();
        }
    }

    Serial.println(
        "Transfer folder fail"
    );

    return false;
}

bool drawNextDuelPersonaFrame(
    File& dir,
    bool& opened,
    bool& done,
    const String& persona,
    int32_t x,
    int32_t y
) {

    if (
        done
    ) {
        return false;
    }

    for (
        uint8_t attempt = 0;
        attempt < 32;
        ++attempt
    ) {
        if (
            !opened
        ) {
            if (
                !openDuelFolder(
                    dir,
                    opened,
                    persona,
                    duelState
                )
            ) {
                return false;
            }
        }

        File file =
            dir.openNextFile();

        if (!file) {
            dir.close();
            opened =
                false;

            if (
                duelOneShot
            ) {
                done =
                    true;
                return false;
            }

            continue;
        }

        String filename =
            file.name();

        if (
            file.isDirectory() ||
            isHiddenSdEntry(
                filename
            ) ||
            (
                !filename.endsWith(
                    ".png"
                ) &&
                !filename.endsWith(
                    ".PNG"
                )
            )
        ) {
            file.close();
            continue;
        }

        size_t size =
            file.size();

        uint8_t* buf =
            psramFound() ?
            (uint8_t*)ps_malloc(
                size
            ) :
            nullptr;

        if (
            !buf
        ) {
            buf =
                (uint8_t*)malloc(
                    size
                );
        }

        if (
            !buf
        ) {
            Serial.print(
                "Duel frame malloc fail: "
            );
            Serial.print(
                filename
            );
            Serial.print(
                " size="
            );
            Serial.println(
                size
            );
            logHeap(
                "duel frame malloc fail"
            );
            file.close();
            continue;
        }

        Serial.print(
            "Duel frame: "
        );
        Serial.print(
            filename
        );
        Serial.print(
            " size="
        );
        Serial.println(
            size
        );
        logHeap(
            "duel frame before draw"
        );

        file.read(
            buf,
            size
        );

        file.close();

        uint32_t imageWidth;
        uint32_t imageHeight;

        readPngSize(
            buf,
            size,
            imageWidth,
            imageHeight
        );

        float scaleX =
            imageWidth > 0 ?
            (float)SCREEN_WIDTH / (float)imageWidth :
            1.0f;

        float scaleY =
            imageHeight > 0 ?
            (float)SCREEN_HEIGHT / (float)imageHeight :
            1.0f;

        bool drewFrame =
            false;
        bool shouldMirror =
            shouldMirrorPersonaOnScreen(
                persona
            ) &&
            mirrorCanvasReady;

        auto drawMirroredFrame = [&]() {
            if (
                drawingToFrameCanvas
            ) {
                return drawMirroredPngToTarget(
                    frameCanvas,
                    buf,
                    size
                );
            }

            return drawMirroredPngToTarget(
                M5.Display,
                buf,
                size
            );
        };

        if (
            shouldMirror
        ) {
            drewFrame =
                drawMirroredFrame();
        } else if (
            drawingToFrameCanvas
        ) {
            drewFrame =
                frameCanvas.drawPng(
                buf,
                size,
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                scaleX,
                scaleY
            );
        } else {
            drewFrame =
                M5.Display.drawPng(
                buf,
                size,
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                scaleX,
                scaleY
            );
        }

        free(buf);

        if (
            drewFrame
        ) {
            return true;
        }
    }

    return false;
}

void finishTransferStage() {

    Serial.print(
        "Duel stage done: "
    );
    Serial.println(
        duelState
    );

    if (
        duelNextState ==
        "idle"
    ) {
        duelMode =
            false;

        duelOneShot =
            false;

        currentState =
            "idle";

        displayText =
            "";

        resetDuelFolders();
        resetAnimationFolder();
        resetTransferFolder();
        clearGiftFlags();
        resetRenderedScene();
        playAnimationFrame();
        return;
    }

    if (
        duelNextState.length() > 0
    ) {
        duelOneShot =
            false;

        duelState =
            duelNextState;

        duelText =
            "";

        resetDuelFolders();
        resetAnimationFolder();
        resetTransferFolder();
        resetRenderedScene();
        playDuelFrame();
        return;
    }
}

void playTransferFrame() {

    if (
        transferHoldActive
    ) {
        if (
            millis() -
            transferHoldStartedAt >=
            TRANSFER_HOLD_MS
        ) {
            finishTransferStage();
        }

        return;
    }

    if (
        !transferOpened
    ) {
        if (
            !openTransferFolder()
        ) {
            finishTransferStage();
        }

        return;
    }

    for (
        uint8_t attempt = 0;
        attempt < 32;
        ++attempt
    ) {
        File file =
            transferDir.openNextFile();

        if (
            !file
        ) {
            transferDone =
                true;

            transferHoldActive =
                true;

            transferHoldStartedAt =
                millis();

            return;
        }

        String filename =
            file.name();

        if (
            file.isDirectory() ||
            isHiddenSdEntry(
                filename
            ) ||
            (
                !filename.endsWith(
                    ".png"
                ) &&
                !filename.endsWith(
                    ".PNG"
                )
            )
        ) {
            file.close();
            continue;
        }

        size_t size =
            file.size();

        uint8_t* buf =
            psramFound() ?
            (uint8_t*)ps_malloc(
                size
            ) :
            nullptr;

        if (
            !buf
        ) {
            buf =
                (uint8_t*)malloc(
                    size
                );
        }

        if (
            !buf
        ) {
            Serial.print(
                "Transfer malloc fail: "
            );
            Serial.print(
                filename
            );
            Serial.print(
                " size="
            );
            Serial.println(
                size
            );
            logHeap(
                "transfer malloc fail"
            );
            file.close();
            continue;
        }

        Serial.print(
            "Transfer frame: "
        );
        Serial.print(
            filename
        );
        Serial.print(
            " size="
        );
        Serial.println(
            size
        );
        logHeap(
            "transfer before draw"
        );

        file.read(
            buf,
            size
        );

        file.close();

        bool drewFrame =
            false;

        bool useCapturedBase =
            overlayCapturedBaseReady;

        if (
            frameCanvasReady
        ) {
            if (
                useCapturedBase
            ) {
                if (
                    !prepareCapturedOverlayBase()
                ) {
                    frameCanvas.fillScreen(
                        BLACK
                    );
                }
            } else if (
                !prepareFullFrameCanvas(
                    currentPersona,
                    transferBackgroundState
                )
            ) {
                frameCanvas.fillScreen(
                    BLACK
                );
            }

            drewFrame =
                frameCanvas.drawPng(
                    buf,
                    size,
                    0,
                    0,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    0,
                    0,
                    1.0f,
                    1.0f
                );

            if (
                drewFrame
            ) {
                frameCanvas.pushSprite(
                    0,
                    0
                );
            }
        } else {
            if (
                useCapturedBase
            ) {
                if (
                    !prepareCapturedOverlayBase()
                ) {
                    M5.Display.fillScreen(
                        BLACK
                    );
                }
            } else if (
                !ensureSceneOnScreen(
                    currentPersona,
                    transferBackgroundState
                )
            ) {
                M5.Display.fillScreen(
                    BLACK
                );
            }

            drewFrame =
                M5.Display.drawPng(
                buf,
                size,
                0,
                0,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                0,
                0,
                1.0f,
                1.0f
            );
        }

        free(
            buf
        );

        if (
            drewFrame
        ) {
            if (
                filename.equalsIgnoreCase(
                    TRANSFER_HOLD_FRAME
                )
            ) {
                drawCenteredTransferText();

                transferDone =
                    true;

                transferHoldActive =
                    true;

                transferHoldStartedAt =
                    millis();

                return;
            }

            delay(
                TRANSFER_FRAME_DELAY
            );

            return;
        }
    }
}

void playDuelFrame() {

    if (
        !isChatLikeDuelState(
            duelState
        )
    ) {
        if (
            isTransferStage(
                duelState
            )
        ) {
            playTransferFrame();
            return;
        }

        currentState =
            duelState;

        playAnimationFrame();

        return;
    }

    drawingToFrameCanvas =
        prepareFullFrameCanvas(
        currentPersona,
        duelState
    );

    bool leftDrew =
        drawNextDuelPersonaFrame(
        duelLeftDir,
        duelLeftOpened,
        duelLeftDone,
        duelLeftPersona,
        10,
        72
    );

    bool rightDrew =
        drawNextDuelPersonaFrame(
        duelRightDir,
        duelRightOpened,
        duelRightDone,
        duelRightPersona,
        164,
        72
    );

    bool composedFrame =
        drawingToFrameCanvas;

    bool frameReady =
        composedFrame &&
        leftDrew &&
        rightDrew;

    bool dialogState =
        shouldDrawDialogBox(
            duelState
        );

    if (
        dialogState &&
        frameReady
    ) {
        cleanFrameCaptureReady =
            captureFrameCanvasToSceneCanvas();

        drawDuelTextBox();
    } else if (
        dialogState &&
        cleanFrameCaptureReady
    ) {
        if (
            frameCanvasReady &&
            frameCanvas.getBuffer() !=
                nullptr &&
            prepareCapturedOverlayBase()
        ) {
            drawingToFrameCanvas =
                true;

            drawDuelTextBox();

            frameCanvas.pushSprite(
                0,
                0
            );
        } else {
            drawingToFrameCanvas =
                false;

            restoreCapturedSceneCanvas();
            drawDuelTextBox();
        }
    }

    if (
        duelOneShot &&
        duelLeftDone &&
        duelRightDone
    ) {
        Serial.print(
            "Duel stage done: "
        );
        Serial.println(
            duelState
        );

        if (
            duelNextState ==
            "idle"
        ) {
            duelMode =
                false;
            duelOneShot =
                false;
            currentState =
                "idle";
            displayText =
                "";
            resetDuelFolders();
            resetAnimationFolder();
            resetRenderedScene();
            playAnimationFrame();
            return;
        }

        if (
            duelNextState.length() > 0
        ) {
            String next =
                duelNextState;

            duelOneShot =
                false;
            duelState =
                next;
            duelText =
                "";
            resetDuelFolders();
            resetRenderedScene();
            playDuelFrame();
            return;
        }
    }

    if (
        !sdReady
    ) {
        M5.Display.fillRoundRect(
            28,
            104,
            86,
            86,
            18,
            DARKGREY
        );
        M5.Display.fillRoundRect(
            206,
            104,
            86,
            86,
            18,
            DARKGREY
        );
        M5.Display.setTextColor(
            WHITE,
            DARKGREY
        );
        M5.Display.setFont(
            &fonts::Font2
        );
        M5.Display.setCursor(
            48,
            142
        );
        M5.Display.print(
            "NO SD"
        );
        M5.Display.setCursor(
            226,
            142
        );
        M5.Display.print(
            "NO SD"
        );
    }

    if (
        frameReady
    ) {
        presentFullFrameCanvas();
        drawingToFrameCanvas =
            false;
    }

    if (
        dialogState &&
        !frameReady
    ) {
        drawingToFrameCanvas =
            false;
    }

    delay(
        frameDelay
    );
}

// ======================
// 播放一帧
// ======================
void playAnimationFrame() {

    if (
        !enableAnimation
    ) {
        if (
            shouldDrawDialogBox(
                currentState
            )
        ) {
            if (
                currentState ==
                "coffee"
            ) {
                lastCoffeeRenderedText =
                    "";
            }

            drawTextBox();
        }
        return;
    }

    if (
        isPlayingAudio
    ) {
        if (
            shouldDrawDialogBox(
                currentState
            )
        ) {
            drawTextBox();
        }
        return;
    }

    if (
        !folderOpened
    ) {
        openAnimationFolder();

        if (
            !folderOpened &&
            currentState ==
            "coffee"
        ) {
            ensureSceneOnScreen(
                currentPersona,
                currentState
            );
            lastCoffeeRenderedText =
                "";
            drawTextBox();
        }

        if (
            !folderOpened
        ) {
            ensureSceneOnScreen(
                currentPersona,
                currentState
            );
        }

        return;
    }

    for (
        uint8_t attempt = 0;
        attempt < 32;
        ++attempt
    ) {
        File file =
            currentDir.openNextFile();

        // 播放完重新循环
        if (!file) {

            currentDir.close();

            folderOpened =
                false;

            animationLooped =
                true;

            openAnimationFolder();

            if (
                !folderOpened
            ) {
                return;
            }

            continue;
        }

        String filename =
            file.name();

        if (
            file.isDirectory() ||
            isHiddenSdEntry(
                filename
            ) ||
            (
                !filename.endsWith(
                    ".png"
                ) &&
                !filename.endsWith(
                    ".PNG"
                )
            )
        ) {
            file.close();
            continue;
        }

        size_t size =
            file.size();

        uint8_t* buf =
            psramFound() ?
            (uint8_t*)ps_malloc(
                size
            ) :
            nullptr;

        if (
            !buf
        ) {
            buf =
                (uint8_t*)malloc(
                    size
                );
        }

        if (
            !buf
        ) {
            Serial.print(
                "Anim malloc fail: "
            );
            Serial.print(
                filename
            );
            Serial.print(
                " size="
            );
            Serial.println(
                size
            );
            logHeap(
                "anim malloc fail"
            );
            file.close();
            continue;
        }

        file.read(
            buf,
            size
        );

        file.close();

        uint32_t imageWidth;
        uint32_t imageHeight;

        readPngSize(
            buf,
            size,
            imageWidth,
            imageHeight
        );

        float scaleX =
            imageWidth > 0 ?
            (float)SCREEN_WIDTH / (float)imageWidth :
            1.0f;

        float scaleY =
            imageHeight > 0 ?
            (float)SCREEN_HEIGHT / (float)imageHeight :
            1.0f;

        bool composed =
            prepareFullFrameCanvas(
                currentPersona,
                currentState
            );

        bool drewFrame =
            false;

        if (
            composed
        ) {
            drewFrame =
                frameCanvas.drawPng(
                    buf,
                    size,
                    0,
                    0,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    0,
                    0,
                    scaleX,
                    scaleY
                );

            if (
                drewFrame
            ) {
                presentFullFrameCanvas();
            }
        } else {
            drewFrame =
                M5.Display.drawPng(
                    buf,
                    size,
                    0,
                    0,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    0,
                    0,
                    scaleX,
                    scaleY
                );
        }

        free(buf);

        if (
            !drewFrame
        ) {
            Serial.print(
                "Anim draw fail: "
            );
            Serial.println(
                filename
            );
            continue;
        }

        static String lastDrawnAnimationFrame =
            "";

        if (
            lastDrawnAnimationFrame !=
            filename
        ) {
            Serial.print(
                "Anim drew frame: "
            );
            Serial.println(
                filename
            );
            lastDrawnAnimationFrame =
                filename;
        }

        if (
            shouldDrawDialogBox(
                currentState
            )
        ) {
            drawTextBox();
        }

        delay(frameDelay);
        return;
    }

    ensureSceneOnScreen(
        currentPersona,
        currentState
    );
    Serial.print(
        "Anim no drawable frame: "
    );
    Serial.println(
        currentState
    );

    if (
        currentState ==
        "idle" &&
        currentPersona ==
        "xiao_hong" &&
        !idleSelfFallbackTried
    ) {
        idleSelfFallbackTried =
            true;

        Serial.println(
            "Fallback idle animation: self"
        );

        resetAnimationFolder();
        openAnimationFolder();
        return;
    }
}

void reportAudioDone() {

    if (
        wsConnected
    ) {
        wsClient.send(
            String(
                "AUDIO_DONE|"
            ) +
            currentPersona
        );
    }
}

void finishAudioPlayback() {

    if (
        activePcmBuffer
    ) {
        free(
            activePcmBuffer
        );
    }

    activePcmBuffer =
        nullptr;

    activePcmSize =
        0;

    activePcmEarliestDoneAt =
        0;

    activePcmForceDoneAt =
        0;

    isPlayingAudio =
        false;

    Serial.println(
        "PCM done"
    );

    reportAudioDone();

    if (
        duelMode
    ) {
        resetDuelFolders();
    } else {
        resetAnimationFolder();
    }

    enableAnimation =
        true;
}

void pumpAudioPlayback() {

    if (
        !isPlayingAudio
    ) {
        return;
    }

    unsigned long now =
        millis();

    if (
        activePcmForceDoneAt > 0 &&
        now >= activePcmForceDoneAt
    ) {
        Serial.println(
            "PCM force done"
        );
        M5.Speaker.stop();
        finishAudioPlayback();
        return;
    }

    if (
        activePcmEarliestDoneAt > 0 &&
        now >= activePcmEarliestDoneAt &&
        !M5.Speaker.isPlaying()
    ) {
        finishAudioPlayback();
    }
}

void playPCMBuffer(
    uint8_t* pcmBuffer,
    uint32_t pcmSize,
    uint32_t sampleRate,
    const char* source
) {

    Serial.print(
        source
    );
    Serial.print(
        " play bytes: "
    );
    Serial.println(
        pcmSize
    );

    if (
        !ENABLE_HARDWARE_AUDIO
    ) {
        Serial.println(
            "PCM muted"
        );

        free(
            pcmBuffer
        );

        finishAudioPlayback();

        return;
    }

    isPlayingAudio =
        true;

    bool queued =
        M5.Speaker.playRaw(
            (const int16_t*)
            pcmBuffer,

            pcmSize / 2,

            sampleRate,

            false,

            1,

            0,

            true
        );

    Serial.print(
        "PCM queued: "
    );
    Serial.println(
        queued ?
        "yes" :
        "no"
    );

    if (
        !queued
    ) {
        Serial.println(
            "PCM queue fail"
        );
        free(
            pcmBuffer
        );
        finishAudioPlayback();
        return;
    }

    unsigned long playMs =
        (unsigned long)(
            ((uint64_t)(pcmSize / 2) * 1000ULL) /
            sampleRate
        );

    activePcmBuffer =
        pcmBuffer;

    activePcmSize =
        pcmSize;

    activePcmEarliestDoneAt =
        millis() +
        playMs;

    activePcmForceDoneAt =
        activePcmEarliestDoneAt +
        12000;
}

void resetWsPcmBuffer() {

    if (
        wsPcmBuffer
    ) {
        free(
            wsPcmBuffer
        );
    }

    wsPcmBuffer =
        nullptr;

    wsPcmSize =
        0;

    wsPcmReceived =
        0;

    wsPcmSampleRate =
        DEFAULT_AUDIO_SAMPLE_RATE;

    wsPcmActive =
        false;
}

void handleWsPcmBegin(
    const String& msg
) {

    int p1 =
        msg.indexOf(
            '|'
        );

    int p2 =
        msg.indexOf(
            '|',
            p1 + 1
        );

    if (
        p1 < 0 ||
        p2 < 0
    ) {
        Serial.println(
            "PCMWSBEGIN parse fail"
        );
        return;
    }

    resetWsPcmBuffer();

    wsPcmSampleRate =
        msg.substring(
            p1 + 1,
            p2
        ).toInt();

    wsPcmSize =
        msg.substring(
            p2 + 1
        ).toInt();

    if (
        wsPcmSampleRate == 0 ||
        wsPcmSize == 0
    ) {
        Serial.println(
            "PCMWSBEGIN invalid"
        );
        resetWsPcmBuffer();
        return;
    }

    wsPcmBuffer =
        (uint8_t*)malloc(
            wsPcmSize
        );

    if (
        !wsPcmBuffer
    ) {
        Serial.println(
            "PCMWS malloc fail"
        );
        resetWsPcmBuffer();
        return;
    }

    wsPcmReceived =
        0;

    wsPcmActive =
        true;

    Serial.print(
        "PCMWS begin: "
    );
    Serial.println(
        wsPcmSize
    );
}

bool decodeWsPcmChunk(
    const String& payload
) {

    if (
        !wsPcmActive ||
        !wsPcmBuffer
    ) {
        Serial.println(
            "PCMWS chunk without begin"
        );
        return false;
    }

    size_t decodedLen =
        0;

    int rc =
        mbedtls_base64_decode(
            wsPcmBuffer + wsPcmReceived,
            wsPcmSize - wsPcmReceived,
            &decodedLen,
            (const unsigned char*)payload.c_str(),
            payload.length()
        );

    if (
        rc != 0
    ) {
        Serial.print(
            "PCMWS decode fail: "
        );
        Serial.println(
            rc
        );
        resetWsPcmBuffer();
        isPlayingAudio =
            false;
        return false;
    }

    wsPcmReceived +=
        decodedLen;

    return true;
}

void handleWsPcmEnd() {

    if (
        !wsPcmActive ||
        !wsPcmBuffer
    ) {
        Serial.println(
            "PCMWS end without begin"
        );
        return;
    }

    if (
        wsPcmReceived !=
        wsPcmSize
    ) {
        Serial.print(
            "PCMWS size mismatch: "
        );
        Serial.print(
            wsPcmReceived
        );
        Serial.print(
            "/"
        );
        Serial.println(
            wsPcmSize
        );
        resetWsPcmBuffer();
        isPlayingAudio =
            false;
        return;
    }

    uint8_t* pcmBuffer =
        wsPcmBuffer;

    uint32_t pcmSize =
        wsPcmSize;

    uint32_t sampleRate =
        wsPcmSampleRate;

    wsPcmBuffer =
        nullptr;

    wsPcmSize =
        0;

    wsPcmReceived =
        0;

    wsPcmActive =
        false;

    Serial.print(
        "PCMWS received: "
    );
    Serial.println(
        pcmSize
    );

    playPCMBuffer(
        pcmBuffer,
        pcmSize,
        sampleRate,
        "PCMWS"
    );
}

void handleWsPcmSingle(
    const String& msg
) {

    int p1 =
        msg.indexOf(
            '|'
        );

    int p2 =
        msg.indexOf(
            '|',
            p1 + 1
        );

    if (
        p1 < 0 ||
        p2 < 0
    ) {
        Serial.println(
            "PCMWS parse fail"
        );
        return;
    }

    uint32_t sampleRate =
        msg.substring(
            p1 + 1,
            p2
        ).toInt();

    String payload =
        msg.substring(
            p2 + 1
        );

    size_t maxDecoded =
        (payload.length() * 3) / 4 + 4;

    uint8_t* pcmBuffer =
        (uint8_t*)malloc(
            maxDecoded
        );

    if (
        !pcmBuffer
    ) {
        Serial.println(
            "PCMWS malloc fail"
        );
        return;
    }

    size_t decodedLen =
        0;

    int rc =
        mbedtls_base64_decode(
            pcmBuffer,
            maxDecoded,
            &decodedLen,
            (const unsigned char*)payload.c_str(),
            payload.length()
        );

    if (
        rc != 0
    ) {
        Serial.print(
            "PCMWS decode fail: "
        );
        Serial.println(
            rc
        );
        free(
            pcmBuffer
        );
        return;
    }

    playPCMBuffer(
        pcmBuffer,
        decodedLen,
        sampleRate,
        "PCMWS"
    );
}

void handleWsPcmMessage(
    const String& msg
) {

    if (
        msg.startsWith(
            "PCMWSBEGIN|"
        )
    ) {
        handleWsPcmBegin(
            msg
        );
        return;
    }

    if (
        msg.startsWith(
            "PCMWSCHUNK|"
        )
    ) {
        decodeWsPcmChunk(
            msg.substring(
                11
            )
        );
        return;
    }

    if (
        msg == "PCMWSEND"
    ) {
        handleWsPcmEnd();
        return;
    }

    if (
        msg.startsWith(
            "PCMWS|"
        )
    ) {
        handleWsPcmSingle(
            msg
        );
        return;
    }

    Serial.println(
        "PCMWS ignored"
    );
}

// ======================
// PCM 接收
// 协议：
// PCM + uint32 + data
// ======================
void receivePCM(
    char magic0,
    char magic1,
    char magic2
) {

    bool isLegacyPcm =
        magic0 == 'P' &&
        magic1 == 'C' &&
        magic2 == 'M';

    bool hasRateHeader =
        magic0 == 'P' &&
        magic1 == 'C' &&
        magic2 == '2';

    if (
        !isLegacyPcm &&
        !hasRateHeader
    ) {
        return;
    }

    uint32_t sampleRate =
        DEFAULT_AUDIO_SAMPLE_RATE;
    uint32_t pcmSize =
        0;

    if (
        hasRateHeader
    ) {

        while (
            Serial.available() < 8
        );

        Serial.readBytes(
            (char*)&sampleRate,
            4
        );

        Serial.readBytes(
            (char*)&pcmSize,
            4
        );
    } else {

        while (
            Serial.available() < 4
        );

        Serial.readBytes(
            (char*)&pcmSize,
            4
        );
    }

    Serial.print(
        "PCM sample rate: "
    );
    Serial.println(
        sampleRate
    );

    Serial.print(
        "PCM bytes: "
    );
    Serial.println(
        pcmSize
    );

    isPlayingAudio =
        true;

    uint8_t* pcmBuffer =
        (uint8_t*)malloc(
            pcmSize
        );

    if (
        !pcmBuffer
    ) {
        Serial.println(
            "PCM malloc fail"
        );
        isPlayingAudio =
            false;
        return;
    }

    Serial.println(
        "PCM READY"
    );

    uint32_t received =
        0;
    unsigned long lastByteAt =
        millis();

    while (
        received <
        pcmSize
    ) {

        int remain =
            pcmSize -
            received;

        int availableBytes =
            Serial.available();

        if (
            availableBytes <= 0
        ) {
            if (
                millis() -
                lastByteAt > 3000
            ) {
                Serial.print(
                    "PCM receive timeout: "
                );
                Serial.println(
                    received
                );
                free(
                    pcmBuffer
                );
                isPlayingAudio =
                    false;
                return;
            }
            delay(1);
            continue;
        }

        int toRead =
            min(
                remain,
                availableBytes
            );

        int readLen =
            Serial.readBytes(
                (char*)pcmBuffer + received,
                toRead
            );

        if (
            readLen > 0
        ) {

            received +=
                readLen;
            lastByteAt =
                millis();
        } else {
            Serial.println(
                "PCM read timeout"
            );
            delay(1);
        }
    }

    Serial.print(
        "PCM received: "
    );
    Serial.println(
        received
    );

    playPCMBuffer(
        pcmBuffer,
        pcmSize,
        sampleRate,
        "PCM"
    );
}

// ======================
// setup
// ======================
void setup() {

    auto cfg =
        M5.config();

    M5.begin(cfg);

    Serial.begin(
        2000000
    );

    Serial.print(
        "Reset reason: "
    );
    Serial.println(
        resetReasonName(
            esp_reset_reason()
        )
    );
    logHeap(
        "boot"
    );

    personaPrefs.begin(
        "persona",
        false
    );

    currentPersona =
        personaPrefs.getString(
            "persona",
            currentPersona
        );

    M5.Display.fillScreen(
        BLACK
    );
    resetRenderedScene();

    sceneCanvas.setColorDepth(
        16
    );

    sceneCanvasReady =
        sceneCanvas.createSprite(
            SCREEN_WIDTH,
            SCREEN_HEIGHT
        ) != nullptr;

    if (
        !sceneCanvasReady
    ) {
        Serial.println(
            "Scene canvas fail"
        );
    }

    frameCanvas.setColorDepth(
        16
    );

    frameCanvasReady =
        frameCanvas.createSprite(
            SCREEN_WIDTH,
            SCREEN_HEIGHT
        ) != nullptr;

    if (
        !frameCanvasReady
    ) {
        Serial.println(
            "Frame canvas fail"
        );
    }

    mirrorCanvasReady =
        false;

    Serial.println(
        "Mirror disabled"
    );

    // ======================
    // Speaker
    // ======================
    auto spk_cfg =
        M5.Speaker.config();

    spk_cfg.sample_rate =
        DEFAULT_AUDIO_SAMPLE_RATE;

    M5.Speaker.config(
        spk_cfg
    );

    M5.Speaker.begin();
    M5.Speaker.setVolume(
        180
    );

    sdReady =
        initSDCard();

    if (
        sdReady
    ) {
        displayUiFontLoaded =
            loadDisplayUiFont();

        frameUiFontLoaded =
            frameCanvasReady &&
            loadFrameUiFont();

        M5.Display.println(
            "SD OK"
        );
    } else {
        Serial.println(
            "SD FAIL"
        );

        M5.Display.println(
            "SD FAIL"
        );
    }

    delay(1000);

    M5.Display.fillScreen(
        BLACK
    );

    nfcBegin();

    ensureSceneOnScreen(
        currentPersona,
        currentState
    );

    openAnimationFolder();
    playAnimationFrame();

    wsBegin();

    unsigned long wifiWaitStart =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() -
        wifiWaitStart <
        8000
    ) {
        wsLoop();
        delay(
            250
        );
    }

    bleBegin();

    Serial.println(
        "READY"
    );

    reportPersona();
    reportPowerStatus();
}

// ======================
// loop
// ======================
void loop() {

    M5.update();

    wsLoop();

    pumpAudioPlayback();

    bleLoop();

    if (
        nfcLoop()
    ) {
        return;
    }

    if (
        millis() - lastPersonaReportAt > 2000
    ) {
        reportPersona();
    }

    if (
        millis() - lastPowerReportAt > 5000
    ) {
        reportPowerStatus();
    }

    if (
        M5.BtnA.wasPressed()
    ) {
        selectPersona(
            "zhang_zong"
        );
    }

    if (
        M5.BtnB.wasPressed()
    ) {
        selectPersona(
            "xiao_hong"
        );
    }

    // ======================
    // 串口消息
    // ======================
    if (
        Serial.available()
    ) {

        char c =
            Serial.peek();

        // PCM
        if (
            c == 'P'
        ) {

            while (
                Serial.available() < 3
            ) {
                delay(1);
            }

            char magic0 =
                Serial.read();

            char magic1 =
                Serial.read();

            char magic2 =
                Serial.read();

            if (
                magic0 == 'P' &&
                magic1 == 'C' &&
                (
                    magic2 == 'M' ||
                    magic2 == '2'
                )
            ) {

                receivePCM(
                    magic0,
                    magic1,
                    magic2
                );
            } else if (
                magic0 == 'P' &&
                magic1 == 'E' &&
                magic2 == 'R'
            ) {

                String rest =
                    Serial.readStringUntil(
                        '\n'
                    );

                processCommandLine(
                    "PER" +
                    rest
                );
            } else {
                Serial.readStringUntil(
                    '\n'
                );
            }

            return;
        }

        if (
            c == 'S'
        ) {

            receivePersonaCommand();

            return;
        }

        if (
            c == 'D'
        ) {

            receiveDuelCommand();

            return;
        }

        // TXT
        if (
            c == 'T'
        ) {

            processCommandLine(
                Serial.readStringUntil(
                    '\n'
                )
            );
        }
    }

    if (
        duelMode
    ) {
        playDuelFrame();
    } else {
        if (
            giftPending &&
            !giftPlaying
        ) {
            giftPending =
                false;

            playGiftSequence();

            if (
                giftStartsTransfer &&
                duelState == "leave"
            ) {
                duelMode =
                    true;

                duelOneShot =
                    false;

                resetTransferFolder();
                resetRenderedScene();
                playTransferFrame();
                return;
            }

            currentState =
                "idle";

            displayText =
                "";

            resetAnimationFolder();
            resetRenderedScene();
            playAnimationFrame();
            return;
        }

        playAnimationFrame();
    }
}

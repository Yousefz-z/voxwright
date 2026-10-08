// Voxwright Virtual Microphone: a loopback audio device for macOS, written
// as an AudioServerPlugIn (a user-space driver loaded by coreaudiod).
//
// One device with an output stream and an input stream. Voxwright plays
// into the output; chat apps record from the input. Audio written for a
// given sample time is read back when the input reaches that sample time,
// through a ring buffer indexed by sample time.
//
// Status: written from Apple's documented AudioServerPlugIn interface
// without access to a Mac. It has not been run. See README.md.

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ------------------------------------------------------------------ constants

enum {
    kObjectPlugIn = kAudioObjectPlugInObject,
    kObjectDevice = 2,
    kObjectInputStream = 3,
    kObjectOutputStream = 4,
};

#define kDeviceUID "io.github.yousefz-z.voxwright.mic"
#define kDeviceModelUID "io.github.yousefz-z.voxwright.mic.model"
#define kDeviceName "Voxwright Virtual Microphone"
#define kManufacturer "Voxwright"

static const UInt32 kChannels = 2;
static const UInt32 kBytesPerFrame = sizeof(Float32) * 2;
/// Ring length and zero-time-stamp period: 16384 frames (341 ms at 48 kHz).
static const UInt32 kRingFrames = 16384;
static const Float64 kSampleRates[] = {44100.0, 48000.0};
static const UInt32 kSampleRateCount = 2;

// ---------------------------------------------------------------------- state

static pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;
static AudioServerPlugInHostRef gHost = NULL;
static UInt32 gRefCount = 0;
static Float64 gSampleRate = 48000.0;
static UInt32 gIOClients = 0;
static bool gInputActive = true;
static bool gOutputActive = true;

// Time stamps, touched only by the IO thread once IO runs.
static Float64 gHostTicksPerFrame = 0.0;
static UInt64 gAnchorHostTime = 0;
static UInt64 gTimeStampCount = 0;

// Loopback ring indexed by sample time modulo kRingFrames. `gWrittenUntil`
// is the sample time just past the last frame written; frames older than
// one ring length, or not yet written, read as silence.
static Float32 gRing[16384 * 2];
static _Atomic Float64 gWrittenUntil = 0.0;

// ----------------------------------------------------------- driver interface

static HRESULT QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG AddRef(void* inDriver);
static ULONG Release(void* inDriver);
static OSStatus Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription,
                             const AudioServerPlugInClientInfo* inClientInfo,
                             AudioObjectID* outDeviceObjectID);
static OSStatus DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef inDriver,
                                   AudioObjectID inDeviceObjectID,
                                   const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                                 AudioObjectID inDeviceObjectID,
                                                 UInt64 inChangeAction, void* inChangeInfo);
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                               AudioObjectID inDeviceObjectID,
                                               UInt64 inChangeAction, void* inChangeInfo);
static Boolean HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                           pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                   pid_t inClientProcessID,
                                   const AudioObjectPropertyAddress* inAddress,
                                   Boolean* outIsSettable);
static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress* inAddress,
                                    UInt32 inQualifierDataSize, const void* inQualifierData,
                                    UInt32* outDataSize);
static OSStatus GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                pid_t inClientProcessID,
                                const AudioObjectPropertyAddress* inAddress,
                                UInt32 inQualifierDataSize, const void* inQualifierData,
                                UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                pid_t inClientProcessID,
                                const AudioObjectPropertyAddress* inAddress,
                                UInt32 inQualifierDataSize, const void* inQualifierData,
                                UInt32 inDataSize, const void* inData);
static OSStatus StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                        UInt32 inClientID);
static OSStatus StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                       UInt32 inClientID);
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver,
                                 AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                 Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef inDriver,
                                  AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                  UInt32 inOperationID, Boolean* outWillDo,
                                  Boolean* outWillDoInPlace);
static OSStatus BeginIOOperation(AudioServerPlugInDriverRef inDriver,
                                 AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                 UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                 const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                              AudioObjectID inStreamObjectID, UInt32 inClientID,
                              UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                              const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer,
                              void* ioSecondaryBuffer);
static OSStatus EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                               UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                               const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

static AudioServerPlugInDriverInterface gInterface = {
    NULL,
    QueryInterface,
    AddRef,
    Release,
    Initialize,
    CreateDevice,
    DestroyDevice,
    AddDeviceClient,
    RemoveDeviceClient,
    PerformDeviceConfigurationChange,
    AbortDeviceConfigurationChange,
    HasProperty,
    IsPropertySettable,
    GetPropertyDataSize,
    GetPropertyData,
    SetPropertyData,
    StartIO,
    StopIO,
    GetZeroTimeStamp,
    WillDoIOOperation,
    BeginIOOperation,
    DoIOOperation,
    EndIOOperation,
};
static AudioServerPlugInDriverInterface* gInterfacePtr = &gInterface;
static AudioServerPlugInDriverRef gDriverRef = &gInterfacePtr;

// -------------------------------------------------------------------- factory

/// Named in Info.plist (CFPlugInFactories); coreaudiod calls it to load us.
void* VoxwrightMicCreate(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);

void* VoxwrightMicCreate(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID) {
    (void)inAllocator;
    if (!CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID)) {
        return NULL;
    }
    return gDriverRef;
}

static HRESULT QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface) {
    if (inDriver != gDriverRef || outInterface == NULL) {
        return kAudioHardwareBadObjectError;
    }
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    if (requested == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    HRESULT result = E_NOINTERFACE;
    if (CFEqual(requested, IUnknownUUID) ||
        CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID)) {
        pthread_mutex_lock(&gStateMutex);
        ++gRefCount;
        pthread_mutex_unlock(&gStateMutex);
        *outInterface = gDriverRef;
        result = S_OK;
    }
    CFRelease(requested);
    return result;
}

static ULONG AddRef(void* inDriver) {
    if (inDriver != gDriverRef) {
        return 0;
    }
    pthread_mutex_lock(&gStateMutex);
    const ULONG count = ++gRefCount;
    pthread_mutex_unlock(&gStateMutex);
    return count;
}

static ULONG Release(void* inDriver) {
    if (inDriver != gDriverRef) {
        return 0;
    }
    pthread_mutex_lock(&gStateMutex);
    if (gRefCount > 0) {
        --gRefCount;
    }
    const ULONG count = gRefCount;
    pthread_mutex_unlock(&gStateMutex);
    return count;
}

// ------------------------------------------------------------- basic methods

static OSStatus Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost) {
    if (inDriver != gDriverRef) {
        return kAudioHardwareBadObjectError;
    }
    gHost = inHost;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    const Float64 ticksPerSecond = 1.0e9 * (Float64)timebase.denom / (Float64)timebase.numer;
    gHostTicksPerFrame = ticksPerSecond / gSampleRate;
    return kAudioHardwareNoError;
}

static OSStatus CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription,
                             const AudioServerPlugInClientInfo* inClientInfo,
                             AudioObjectID* outDeviceObjectID) {
    (void)inDriver;
    (void)inDescription;
    (void)inClientInfo;
    (void)outDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError; // the device is fixed
}

static OSStatus DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID) {
    (void)inDriver;
    (void)inDeviceObjectID;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                                const AudioServerPlugInClientInfo* inClientInfo) {
    (void)inClientInfo;
    if (inDriver != gDriverRef) {
        return kAudioHardwareBadObjectError;
    }
    return inDeviceObjectID == kObjectDevice ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef inDriver,
                                   AudioObjectID inDeviceObjectID,
                                   const AudioServerPlugInClientInfo* inClientInfo) {
    (void)inClientInfo;
    if (inDriver != gDriverRef) {
        return kAudioHardwareBadObjectError;
    }
    return inDeviceObjectID == kObjectDevice ? kAudioHardwareNoError : kAudioHardwareBadObjectError;
}

static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                                 AudioObjectID inDeviceObjectID,
                                                 UInt64 inChangeAction, void* inChangeInfo) {
    (void)inChangeInfo;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    // The only change this device requests is a new sample rate, whose
    // index in kSampleRates is the change action.
    if (inChangeAction >= kSampleRateCount) {
        return kAudioHardwareBadObjectError;
    }
    pthread_mutex_lock(&gStateMutex);
    gSampleRate = kSampleRates[inChangeAction];
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    gHostTicksPerFrame = 1.0e9 * (Float64)timebase.denom / (Float64)timebase.numer / gSampleRate;
    memset(gRing, 0, sizeof(gRing));
    atomic_store(&gWrittenUntil, 0.0);
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver,
                                               AudioObjectID inDeviceObjectID,
                                               UInt64 inChangeAction, void* inChangeInfo) {
    (void)inChangeAction;
    (void)inChangeInfo;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    return kAudioHardwareNoError;
}

// ------------------------------------------------------------------ helpers

static AudioStreamBasicDescription StreamFormat(Float64 sampleRate) {
    AudioStreamBasicDescription format;
    memset(&format, 0, sizeof(format));
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = kBytesPerFrame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = kBytesPerFrame;
    format.mChannelsPerFrame = kChannels;
    format.mBitsPerChannel = 32;
    return format;
}

static bool IsStream(AudioObjectID inObjectID) {
    return inObjectID == kObjectInputStream || inObjectID == kObjectOutputStream;
}

/// Copies `size` bytes of `value` to the caller if there is room.
static OSStatus Put(const void* value, UInt32 size, UInt32 inDataSize, UInt32* outDataSize,
                    void* outData) {
    if (inDataSize < size) {
        return kAudioHardwareBadPropertySizeError;
    }
    memcpy(outData, value, size);
    *outDataSize = size;
    return kAudioHardwareNoError;
}

static OSStatus PutString(const char* text, UInt32 inDataSize, UInt32* outDataSize, void* outData) {
    if (inDataSize < sizeof(CFStringRef)) {
        return kAudioHardwareBadPropertySizeError;
    }
    // The caller releases the string.
    CFStringRef value = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    *(CFStringRef*)outData = value;
    *outDataSize = sizeof(CFStringRef);
    return kAudioHardwareNoError;
}

// --------------------------------------------------------------- properties

static Boolean HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                           pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress) {
    (void)inClientProcessID;
    if (inDriver != gDriverRef || inAddress == NULL) {
        return false;
    }
    switch (inObjectID) {
    case kObjectPlugIn:
        switch (inAddress->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
            return true;
        default:
            return false;
        }
    case kObjectDevice:
        switch (inAddress->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertyStreams:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
            return true;
        default:
            return false;
        }
    case kObjectInputStream:
    case kObjectOutputStream:
        switch (inAddress->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

static OSStatus IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                   pid_t inClientProcessID,
                                   const AudioObjectPropertyAddress* inAddress,
                                   Boolean* outIsSettable) {
    if (!HasProperty(inDriver, inObjectID, inClientProcessID, inAddress) || outIsSettable == NULL) {
        return kAudioHardwareUnknownPropertyError;
    }
    switch (inAddress->mSelector) {
    case kAudioDevicePropertyNominalSampleRate:
        *outIsSettable = inObjectID == kObjectDevice;
        break;
    case kAudioStreamPropertyIsActive:
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
        *outIsSettable = IsStream(inObjectID);
        break;
    default:
        *outIsSettable = false;
        break;
    }
    return kAudioHardwareNoError;
}

/// Number of streams in `scope` (input, output, or both).
static UInt32 StreamsInScope(AudioObjectPropertyScope scope, AudioObjectID* outIDs) {
    UInt32 count = 0;
    if (scope == kAudioObjectPropertyScopeGlobal || scope == kAudioObjectPropertyScopeInput) {
        if (outIDs != NULL) {
            outIDs[count] = kObjectInputStream;
        }
        ++count;
    }
    if (scope == kAudioObjectPropertyScopeGlobal || scope == kAudioObjectPropertyScopeOutput) {
        if (outIDs != NULL) {
            outIDs[count] = kObjectOutputStream;
        }
        ++count;
    }
    return count;
}

static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                    pid_t inClientProcessID,
                                    const AudioObjectPropertyAddress* inAddress,
                                    UInt32 inQualifierDataSize, const void* inQualifierData,
                                    UInt32* outDataSize) {
    (void)inQualifierDataSize;
    (void)inQualifierData;
    if (!HasProperty(inDriver, inObjectID, inClientProcessID, inAddress) || outDataSize == NULL) {
        return kAudioHardwareUnknownPropertyError;
    }
    switch (inAddress->mSelector) {
    case kAudioObjectPropertyBaseClass:
    case kAudioObjectPropertyClass:
        *outDataSize = sizeof(AudioClassID);
        return kAudioHardwareNoError;
    case kAudioObjectPropertyOwner:
    case kAudioPlugInPropertyTranslateUIDToDevice:
        *outDataSize = sizeof(AudioObjectID);
        return kAudioHardwareNoError;
    case kAudioObjectPropertyName:
    case kAudioObjectPropertyManufacturer:
    case kAudioDevicePropertyDeviceUID:
    case kAudioDevicePropertyModelUID:
    case kAudioPlugInPropertyResourceBundle:
        *outDataSize = sizeof(CFStringRef);
        return kAudioHardwareNoError;
    case kAudioObjectPropertyOwnedObjects:
        if (inObjectID == kObjectPlugIn) {
            *outDataSize = sizeof(AudioObjectID);
        } else if (inObjectID == kObjectDevice) {
            *outDataSize = StreamsInScope(inAddress->mScope, NULL) * sizeof(AudioObjectID);
        } else {
            *outDataSize = 0;
        }
        return kAudioHardwareNoError;
    case kAudioPlugInPropertyDeviceList:
    case kAudioDevicePropertyRelatedDevices:
        *outDataSize = sizeof(AudioObjectID);
        return kAudioHardwareNoError;
    case kAudioObjectPropertyControlList:
        *outDataSize = 0;
        return kAudioHardwareNoError;
    case kAudioDevicePropertyStreams:
        *outDataSize = StreamsInScope(inAddress->mScope, NULL) * sizeof(AudioObjectID);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyNominalSampleRate:
        *outDataSize = sizeof(Float64);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyAvailableNominalSampleRates:
        *outDataSize = kSampleRateCount * sizeof(AudioValueRange);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyPreferredChannelsForStereo:
        *outDataSize = 2 * sizeof(UInt32);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyPreferredChannelLayout:
        *outDataSize = (UInt32)(offsetof(AudioChannelLayout, mChannelDescriptions) +
                                kChannels * sizeof(AudioChannelDescription));
        return kAudioHardwareNoError;
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
        *outDataSize = sizeof(AudioStreamBasicDescription);
        return kAudioHardwareNoError;
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:
        *outDataSize = kSampleRateCount * sizeof(AudioStreamRangedDescription);
        return kAudioHardwareNoError;
    default:
        *outDataSize = sizeof(UInt32);
        return kAudioHardwareNoError;
    }
}

static OSStatus GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                pid_t inClientProcessID,
                                const AudioObjectPropertyAddress* inAddress,
                                UInt32 inQualifierDataSize, const void* inQualifierData,
                                UInt32 inDataSize, UInt32* outDataSize, void* outData) {
    if (!HasProperty(inDriver, inObjectID, inClientProcessID, inAddress) || outDataSize == NULL ||
        outData == NULL) {
        return kAudioHardwareUnknownPropertyError;
    }
    const AudioObjectPropertySelector selector = inAddress->mSelector;
    UInt32 u32 = 0;
    AudioObjectID id = kAudioObjectUnknown;
    AudioClassID classID = kAudioObjectClassID;

    // Selectors shared by every object.
    if (selector == kAudioObjectPropertyBaseClass) {
        return Put(&classID, sizeof(classID), inDataSize, outDataSize, outData);
    }
    if (selector == kAudioObjectPropertyClass) {
        classID = inObjectID == kObjectPlugIn   ? kAudioPlugInClassID
                  : inObjectID == kObjectDevice ? kAudioDeviceClassID
                                                : kAudioStreamClassID;
        return Put(&classID, sizeof(classID), inDataSize, outDataSize, outData);
    }
    if (selector == kAudioObjectPropertyOwner) {
        id = inObjectID == kObjectPlugIn   ? kAudioObjectUnknown
             : inObjectID == kObjectDevice ? kObjectPlugIn
                                           : kObjectDevice;
        return Put(&id, sizeof(id), inDataSize, outDataSize, outData);
    }
    if (selector == kAudioObjectPropertyManufacturer) {
        return PutString(kManufacturer, inDataSize, outDataSize, outData);
    }

    if (inObjectID == kObjectPlugIn) {
        switch (selector) {
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
            id = kObjectDevice;
            return Put(&id, sizeof(id), inDataSize, outDataSize, outData);
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            if (inQualifierDataSize != sizeof(CFStringRef) || inQualifierData == NULL) {
                return kAudioHardwareBadPropertySizeError;
            }
            CFStringRef uid = *(const CFStringRef*)inQualifierData;
            CFStringRef ours = CFSTR(kDeviceUID);
            id = CFStringCompare(uid, ours, 0) == kCFCompareEqualTo ? kObjectDevice
                                                                    : kAudioObjectUnknown;
            return Put(&id, sizeof(id), inDataSize, outDataSize, outData);
        }
        case kAudioPlugInPropertyResourceBundle:
            return PutString("", inDataSize, outDataSize, outData);
        default:
            return kAudioHardwareUnknownPropertyError;
        }
    }

    if (inObjectID == kObjectDevice) {
        switch (selector) {
        case kAudioObjectPropertyName:
            return PutString(kDeviceName, inDataSize, outDataSize, outData);
        case kAudioDevicePropertyDeviceUID:
            return PutString(kDeviceUID, inDataSize, outDataSize, outData);
        case kAudioDevicePropertyModelUID:
            return PutString(kDeviceModelUID, inDataSize, outDataSize, outData);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: {
            AudioObjectID ids[2];
            const UInt32 count = StreamsInScope(inAddress->mScope, ids);
            const UInt32 room = inDataSize / sizeof(AudioObjectID);
            const UInt32 n = count < room ? count : room;
            memcpy(outData, ids, n * sizeof(AudioObjectID));
            *outDataSize = n * sizeof(AudioObjectID);
            return kAudioHardwareNoError;
        }
        case kAudioObjectPropertyControlList:
            *outDataSize = 0;
            return kAudioHardwareNoError;
        case kAudioDevicePropertyTransportType:
            u32 = kAudioDeviceTransportTypeVirtual;
            return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyRelatedDevices:
            id = kObjectDevice;
            return Put(&id, sizeof(id), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            u32 = 0;
            return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            u32 = 1;
            return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyDeviceIsRunning:
            pthread_mutex_lock(&gStateMutex);
            u32 = gIOClients > 0 ? 1 : 0;
            pthread_mutex_unlock(&gStateMutex);
            return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyZeroTimeStampPeriod:
            u32 = kRingFrames;
            return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
        case kAudioDevicePropertyNominalSampleRate: {
            pthread_mutex_lock(&gStateMutex);
            const Float64 rate = gSampleRate;
            pthread_mutex_unlock(&gStateMutex);
            return Put(&rate, sizeof(rate), inDataSize, outDataSize, outData);
        }
        case kAudioDevicePropertyAvailableNominalSampleRates: {
            AudioValueRange ranges[2];
            for (UInt32 i = 0; i < kSampleRateCount; ++i) {
                ranges[i].mMinimum = kSampleRates[i];
                ranges[i].mMaximum = kSampleRates[i];
            }
            const UInt32 room = inDataSize / sizeof(AudioValueRange);
            const UInt32 n = kSampleRateCount < room ? kSampleRateCount : room;
            memcpy(outData, ranges, n * sizeof(AudioValueRange));
            *outDataSize = n * sizeof(AudioValueRange);
            return kAudioHardwareNoError;
        }
        case kAudioDevicePropertyPreferredChannelsForStereo: {
            const UInt32 channels[2] = {1, 2};
            return Put(channels, sizeof(channels), inDataSize, outDataSize, outData);
        }
        case kAudioDevicePropertyPreferredChannelLayout: {
            const UInt32 size = (UInt32)(offsetof(AudioChannelLayout, mChannelDescriptions) +
                                         kChannels * sizeof(AudioChannelDescription));
            if (inDataSize < size) {
                return kAudioHardwareBadPropertySizeError;
            }
            AudioChannelLayout* layout = (AudioChannelLayout*)outData;
            memset(layout, 0, size);
            layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
            layout->mNumberChannelDescriptions = kChannels;
            layout->mChannelDescriptions[0].mChannelLabel = kAudioChannelLabel_Left;
            layout->mChannelDescriptions[1].mChannelLabel = kAudioChannelLabel_Right;
            *outDataSize = size;
            return kAudioHardwareNoError;
        }
        default:
            return kAudioHardwareUnknownPropertyError;
        }
    }

    // Streams.
    const bool input = inObjectID == kObjectInputStream;
    switch (selector) {
    case kAudioObjectPropertyOwnedObjects:
        *outDataSize = 0;
        return kAudioHardwareNoError;
    case kAudioStreamPropertyIsActive:
        pthread_mutex_lock(&gStateMutex);
        u32 = (input ? gInputActive : gOutputActive) ? 1 : 0;
        pthread_mutex_unlock(&gStateMutex);
        return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
    case kAudioStreamPropertyDirection:
        u32 = input ? 1 : 0;
        return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
    case kAudioStreamPropertyTerminalType:
        u32 = input ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker;
        return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
    case kAudioStreamPropertyStartingChannel:
        u32 = 1;
        return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
    case kAudioStreamPropertyLatency:
        u32 = 0;
        return Put(&u32, sizeof(u32), inDataSize, outDataSize, outData);
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
        pthread_mutex_lock(&gStateMutex);
        const AudioStreamBasicDescription format = StreamFormat(gSampleRate);
        pthread_mutex_unlock(&gStateMutex);
        return Put(&format, sizeof(format), inDataSize, outDataSize, outData);
    }
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats: {
        AudioStreamRangedDescription formats[2];
        for (UInt32 i = 0; i < kSampleRateCount; ++i) {
            formats[i].mFormat = StreamFormat(kSampleRates[i]);
            formats[i].mSampleRateRange.mMinimum = kSampleRates[i];
            formats[i].mSampleRateRange.mMaximum = kSampleRates[i];
        }
        const UInt32 room = inDataSize / sizeof(AudioStreamRangedDescription);
        const UInt32 n = kSampleRateCount < room ? kSampleRateCount : room;
        memcpy(outData, formats, n * sizeof(AudioStreamRangedDescription));
        *outDataSize = n * sizeof(AudioStreamRangedDescription);
        return kAudioHardwareNoError;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

/// Asks the host to switch the sample rate; the switch happens in
/// PerformDeviceConfigurationChange once IO has stopped.
static OSStatus RequestSampleRate(Float64 rate) {
    for (UInt32 i = 0; i < kSampleRateCount; ++i) {
        if (kSampleRates[i] == rate) {
            pthread_mutex_lock(&gStateMutex);
            const bool same = gSampleRate == rate;
            pthread_mutex_unlock(&gStateMutex);
            if (same) {
                return kAudioHardwareNoError;
            }
            return gHost->RequestDeviceConfigurationChange(gHost, kObjectDevice, i, NULL);
        }
    }
    return kAudioDeviceUnsupportedFormatError;
}

static OSStatus SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                                pid_t inClientProcessID,
                                const AudioObjectPropertyAddress* inAddress,
                                UInt32 inQualifierDataSize, const void* inQualifierData,
                                UInt32 inDataSize, const void* inData) {
    (void)inQualifierDataSize;
    (void)inQualifierData;
    Boolean settable = false;
    if (IsPropertySettable(inDriver, inObjectID, inClientProcessID, inAddress, &settable) !=
            kAudioHardwareNoError ||
        !settable || inData == NULL) {
        return kAudioHardwareUnsupportedOperationError;
    }
    switch (inAddress->mSelector) {
    case kAudioDevicePropertyNominalSampleRate:
        if (inDataSize != sizeof(Float64)) {
            return kAudioHardwareBadPropertySizeError;
        }
        return RequestSampleRate(*(const Float64*)inData);
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
        if (inDataSize != sizeof(AudioStreamBasicDescription)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const AudioStreamBasicDescription* format = (const AudioStreamBasicDescription*)inData;
        const AudioStreamBasicDescription ours = StreamFormat(format->mSampleRate);
        if (format->mFormatID != ours.mFormatID || format->mFormatFlags != ours.mFormatFlags ||
            format->mChannelsPerFrame != ours.mChannelsPerFrame ||
            format->mBitsPerChannel != ours.mBitsPerChannel) {
            return kAudioDeviceUnsupportedFormatError;
        }
        return RequestSampleRate(format->mSampleRate);
    }
    case kAudioStreamPropertyIsActive: {
        if (inDataSize != sizeof(UInt32)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const bool active = *(const UInt32*)inData != 0;
        pthread_mutex_lock(&gStateMutex);
        if (inObjectID == kObjectInputStream) {
            gInputActive = active;
        } else {
            gOutputActive = active;
        }
        pthread_mutex_unlock(&gStateMutex);
        const AudioObjectPropertyAddress changed = {kAudioStreamPropertyIsActive,
                                                    kAudioObjectPropertyScopeGlobal,
                                                    kAudioObjectPropertyElementMain};
        gHost->PropertiesChanged(gHost, inObjectID, 1, &changed);
        return kAudioHardwareNoError;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

// ------------------------------------------------------------------------ IO

static OSStatus StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                        UInt32 inClientID) {
    (void)inClientID;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    pthread_mutex_lock(&gStateMutex);
    if (gIOClients == 0) {
        gTimeStampCount = 0;
        gAnchorHostTime = mach_absolute_time();
        memset(gRing, 0, sizeof(gRing));
        atomic_store(&gWrittenUntil, 0.0);
    }
    ++gIOClients;
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

static OSStatus StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                       UInt32 inClientID) {
    (void)inClientID;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    pthread_mutex_lock(&gStateMutex);
    if (gIOClients > 0) {
        --gIOClients;
    }
    pthread_mutex_unlock(&gStateMutex);
    return kAudioHardwareNoError;
}

static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver,
                                 AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                 Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed) {
    (void)inClientID;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    // The device clock runs at exactly the nominal rate on the host clock:
    // one zero time stamp per ring length.
    const Float64 ticksPerPeriod = gHostTicksPerFrame * (Float64)kRingFrames;
    const UInt64 now = mach_absolute_time();
    const UInt64 next = gAnchorHostTime + (UInt64)((Float64)(gTimeStampCount + 1) * ticksPerPeriod);
    if (next <= now) {
        ++gTimeStampCount;
    }
    *outSampleTime = (Float64)(gTimeStampCount * kRingFrames);
    *outHostTime = gAnchorHostTime + (UInt64)((Float64)gTimeStampCount * ticksPerPeriod);
    *outSeed = 1;
    return kAudioHardwareNoError;
}

static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef inDriver,
                                  AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                  UInt32 inOperationID, Boolean* outWillDo,
                                  Boolean* outWillDoInPlace) {
    (void)inClientID;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice) {
        return kAudioHardwareBadObjectError;
    }
    const bool ours = inOperationID == kAudioServerPlugInIOOperationReadInput ||
                      inOperationID == kAudioServerPlugInIOOperationWriteMix;
    if (outWillDo != NULL) {
        *outWillDo = ours;
    }
    if (outWillDoInPlace != NULL) {
        *outWillDoInPlace = true;
    }
    return kAudioHardwareNoError;
}

static OSStatus BeginIOOperation(AudioServerPlugInDriverRef inDriver,
                                 AudioObjectID inDeviceObjectID, UInt32 inClientID,
                                 UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                                 const AudioServerPlugInIOCycleInfo* inIOCycleInfo) {
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return inDriver == gDriverRef && inDeviceObjectID == kObjectDevice
               ? kAudioHardwareNoError
               : kAudioHardwareBadObjectError;
}

static OSStatus DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                              AudioObjectID inStreamObjectID, UInt32 inClientID,
                              UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                              const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer,
                              void* ioSecondaryBuffer) {
    (void)inClientID;
    (void)ioSecondaryBuffer;
    if (inDriver != gDriverRef || inDeviceObjectID != kObjectDevice || ioMainBuffer == NULL ||
        inIOCycleInfo == NULL) {
        return kAudioHardwareBadObjectError;
    }
    Float32* buffer = (Float32*)ioMainBuffer;
    if (inOperationID == kAudioServerPlugInIOOperationWriteMix &&
        inStreamObjectID == kObjectOutputStream) {
        const Float64 start = inIOCycleInfo->mOutputTime.mSampleTime;
        if (start < 0.0) {
            return kAudioHardwareNoError; // before the clock's first period
        }
        for (UInt32 f = 0; f < inIOBufferFrameSize; ++f) {
            const UInt64 slot = (UInt64)(start + f) % kRingFrames;
            for (UInt32 c = 0; c < kChannels; ++c) {
                gRing[slot * kChannels + c] = buffer[f * kChannels + c];
            }
        }
        atomic_store(&gWrittenUntil, start + inIOBufferFrameSize);
    } else if (inOperationID == kAudioServerPlugInIOOperationReadInput &&
               inStreamObjectID == kObjectInputStream) {
        const Float64 start = inIOCycleInfo->mInputTime.mSampleTime;
        const Float64 writtenUntil = atomic_load(&gWrittenUntil);
        for (UInt32 f = 0; f < inIOBufferFrameSize; ++f) {
            const Float64 t = start + f;
            // Only frames written within the last ring length are valid.
            const bool valid = t >= 0.0 && t < writtenUntil && t >= writtenUntil - kRingFrames;
            const UInt64 slot = valid ? (UInt64)t % kRingFrames : 0;
            for (UInt32 c = 0; c < kChannels; ++c) {
                buffer[f * kChannels + c] = valid ? gRing[slot * kChannels + c] : 0.0F;
            }
        }
    }
    return kAudioHardwareNoError;
}

static OSStatus EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID,
                               UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                               const AudioServerPlugInIOCycleInfo* inIOCycleInfo) {
    (void)inClientID;
    (void)inOperationID;
    (void)inIOBufferFrameSize;
    (void)inIOCycleInfo;
    return inDriver == gDriverRef && inDeviceObjectID == kObjectDevice
               ? kAudioHardwareNoError
               : kAudioHardwareBadObjectError;
}

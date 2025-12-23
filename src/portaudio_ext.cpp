#include <portaudio.h>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

// SampleFormat enum
enum class SampleFormat { FLOAT32, INT32, INT24, INT16, INT8, UINT8 };

// SampleFormat を PaSampleFormat に変換
static PaSampleFormat to_pa_format(SampleFormat format) {
  switch (format) {
    case SampleFormat::FLOAT32:
      return paFloat32;
    case SampleFormat::INT32:
      return paInt32;
    case SampleFormat::INT24:
      return paInt24;
    case SampleFormat::INT16:
      return paInt16;
    case SampleFormat::INT8:
      return paInt8;
    case SampleFormat::UINT8:
      return paUInt8;
    default:
      return paFloat32;
  }
}

// PortAudio 初期化状態 (Free-Threading 対応)
static std::mutex pa_init_mutex;
static std::atomic<bool> pa_initialized{false};
static std::atomic<bool> atexit_registered{false};

// PortAudio をクリーンアップ (前方宣言)
static void cleanup_pa();

// PortAudio を初期化 (未初期化の場合のみ、スレッドセーフ)
// 遅延初期化: 実際に PortAudio を使う時に初期化される
static void ensure_pa_init() {
  if (pa_initialized.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard<std::mutex> lock(pa_init_mutex);
  if (pa_initialized.load(std::memory_order_relaxed)) {
    return;
  }
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    throw std::runtime_error(std::string("Failed to initialize PortAudio: ") +
                             Pa_GetErrorText(err));
  }
  pa_initialized.store(true, std::memory_order_release);

  // atexit ハンドラを登録 (一度だけ)
  if (!atexit_registered.exchange(true)) {
    auto atexit = nb::module_::import_("atexit");
    atexit.attr("register")(nb::cpp_function([]() { cleanup_pa(); }));
  }
}

// PortAudio をクリーンアップ
static void cleanup_pa() {
  std::lock_guard<std::mutex> lock(pa_init_mutex);
  if (pa_initialized.load(std::memory_order_relaxed)) {
    Pa_Terminate();
    pa_initialized.store(false, std::memory_order_release);
  }
}

// PortAudio エラーチェックヘルパー
static void check_pa_error(PaError err, const char* context) {
  if (err != paNoError) {
    throw std::runtime_error(std::string(context) + ": " +
                             Pa_GetErrorText(err));
  }
}

// DeviceInfo ラッパー (index を含む)
struct DeviceInfoWrapper {
  PaDeviceIndex index;
  std::string name;
  PaHostApiIndex host_api;
  int max_input_channels;
  int max_output_channels;
  double default_low_input_latency;
  double default_low_output_latency;
  double default_high_input_latency;
  double default_high_output_latency;
  double default_sample_rate;

  static DeviceInfoWrapper from_pa(PaDeviceIndex idx,
                                   const PaDeviceInfo* info) {
    DeviceInfoWrapper wrapper;
    wrapper.index = idx;
    wrapper.name = info->name ? info->name : "";
    wrapper.host_api = info->hostApi;
    wrapper.max_input_channels = info->maxInputChannels;
    wrapper.max_output_channels = info->maxOutputChannels;
    wrapper.default_low_input_latency = info->defaultLowInputLatency;
    wrapper.default_low_output_latency = info->defaultLowOutputLatency;
    wrapper.default_high_input_latency = info->defaultHighInputLatency;
    wrapper.default_high_output_latency = info->defaultHighOutputLatency;
    wrapper.default_sample_rate = info->defaultSampleRate;
    return wrapper;
  }
};

// StreamParameters クラス (Python 側で使いやすくするためのラッパー)
class StreamParameters {
 public:
  PaDeviceIndex device;
  int channel_count;
  PaSampleFormat sample_format;
  PaTime suggested_latency;

  StreamParameters(PaDeviceIndex device,
                   int channel_count,
                   PaSampleFormat sample_format,
                   PaTime suggested_latency)
      : device(device),
        channel_count(channel_count),
        sample_format(sample_format),
        suggested_latency(suggested_latency) {}

  PaStreamParameters to_pa() const {
    PaStreamParameters params;
    params.device = device;
    params.channelCount = channel_count;
    params.sampleFormat = sample_format;
    params.suggestedLatency = suggested_latency;
    params.hostApiSpecificStreamInfo = nullptr;
    return params;
  }
};

// Stream クラス (blocking read/write モード)
class Stream {
 public:
  Stream(std::optional<StreamParameters> input_params,
         std::optional<StreamParameters> output_params,
         double sample_rate,
         unsigned long frames_per_buffer,
         PaStreamFlags stream_flags)
      : stream_(nullptr), is_active_(false) {
    const PaStreamParameters* input_ptr = nullptr;
    const PaStreamParameters* output_ptr = nullptr;

    PaStreamParameters input_pa, output_pa;

    if (input_params.has_value()) {
      input_pa = input_params->to_pa();
      input_ptr = &input_pa;
      input_channels_ = input_params->channel_count;
      input_format_ = input_params->sample_format;
    }

    if (output_params.has_value()) {
      output_pa = output_params->to_pa();
      output_ptr = &output_pa;
      output_channels_ = output_params->channel_count;
      output_format_ = output_params->sample_format;
    }

    sample_rate_ = sample_rate;

    PaError err =
        Pa_OpenStream(&stream_, input_ptr, output_ptr, sample_rate,
                      frames_per_buffer, stream_flags, nullptr, nullptr);
    check_pa_error(err, "Failed to open stream");
  }

  ~Stream() { close(); }

  void start() {
    if (stream_ && !is_active_) {
      PaError err = Pa_StartStream(stream_);
      check_pa_error(err, "Failed to start stream");
      is_active_ = true;
    }
  }

  void stop() {
    if (stream_ && is_active_) {
      PaError err = Pa_StopStream(stream_);
      check_pa_error(err, "Failed to stop stream");
      is_active_ = false;
    }
  }

  void abort() {
    if (stream_ && is_active_) {
      PaError err = Pa_AbortStream(stream_);
      check_pa_error(err, "Failed to abort stream");
      is_active_ = false;
    }
  }

  void close() {
    if (stream_) {
      if (is_active_) {
        Pa_AbortStream(stream_);
        is_active_ = false;
      }
      Pa_CloseStream(stream_);
      stream_ = nullptr;
    }
  }

  bool is_stopped() const {
    if (!stream_) {
      return true;
    }
    return Pa_IsStreamStopped(stream_) == 1;
  }

  bool is_active() const {
    if (!stream_) {
      return false;
    }
    return Pa_IsStreamActive(stream_) == 1;
  }

  PaTime get_time() const {
    if (!stream_) {
      return 0.0;
    }
    return Pa_GetStreamTime(stream_);
  }

  double get_cpu_load() const {
    if (!stream_) {
      return 0.0;
    }
    return Pa_GetStreamCpuLoad(stream_);
  }

  std::optional<PaStreamInfo> get_info() const {
    if (!stream_) {
      return std::nullopt;
    }
    const PaStreamInfo* info = Pa_GetStreamInfo(stream_);
    if (info) {
      return *info;
    }
    return std::nullopt;
  }

  signed long get_read_available() const {
    if (!stream_) {
      return 0;
    }
    return Pa_GetStreamReadAvailable(stream_);
  }

  signed long get_write_available() const {
    if (!stream_) {
      return 0;
    }
    return Pa_GetStreamWriteAvailable(stream_);
  }

  // 統一 read メソッド (フォーマットに応じて自動で型を返す)
  nb::object read(unsigned long frames) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    switch (input_format_) {
      case paFloat32:
        return nb::cast(read_float32(frames));
      case paInt32:
        return nb::cast(read_int32(frames));
      case paInt16:
        return nb::cast(read_int16(frames));
      case paUInt8:
        return nb::cast(read_uint8(frames));
      default:
        throw std::runtime_error("Unsupported sample format for read");
    }
  }

  // 統一 write メソッド (入力の型に応じて自動で処理)
  void write(nb::ndarray<nb::numpy> buffer) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    unsigned long frames = buffer.shape(0);
    const void* data = buffer.data();
    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_WriteStream(stream_, data, frames);
    }
    if (err != paNoError && err != paOutputUnderflowed) {
      check_pa_error(err, "Failed to write stream");
    }
  }

  // float32 フォーマット用の read (shape: [frames, channels])
  nb::ndarray<nb::numpy, float, nb::ndim<2>> read_float32(
      unsigned long frames) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    size_t total_samples = frames * input_channels_;
    float* data = new float[total_samples];

    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_ReadStream(stream_, data, frames);
    }
    if (err != paNoError && err != paInputOverflowed) {
      delete[] data;
      check_pa_error(err, "Failed to read stream");
    }

    size_t shape[2] = {frames, static_cast<size_t>(input_channels_)};
    nb::capsule owner(
        data, [](void* p) noexcept { delete[] static_cast<float*>(p); });

    return nb::ndarray<nb::numpy, float, nb::ndim<2>>(data, 2, shape, owner);
  }

  // int16 フォーマット用の read (shape: [frames, channels])
  nb::ndarray<nb::numpy, int16_t, nb::ndim<2>> read_int16(
      unsigned long frames) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    size_t total_samples = frames * input_channels_;
    int16_t* data = new int16_t[total_samples];

    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_ReadStream(stream_, data, frames);
    }
    if (err != paNoError && err != paInputOverflowed) {
      delete[] data;
      check_pa_error(err, "Failed to read stream");
    }

    size_t shape[2] = {frames, static_cast<size_t>(input_channels_)};
    nb::capsule owner(
        data, [](void* p) noexcept { delete[] static_cast<int16_t*>(p); });

    return nb::ndarray<nb::numpy, int16_t, nb::ndim<2>>(data, 2, shape, owner);
  }

  // int32 フォーマット用の read (shape: [frames, channels])
  nb::ndarray<nb::numpy, int32_t, nb::ndim<2>> read_int32(
      unsigned long frames) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    size_t total_samples = frames * input_channels_;
    int32_t* data = new int32_t[total_samples];

    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_ReadStream(stream_, data, frames);
    }
    if (err != paNoError && err != paInputOverflowed) {
      delete[] data;
      check_pa_error(err, "Failed to read stream");
    }

    size_t shape[2] = {frames, static_cast<size_t>(input_channels_)};
    nb::capsule owner(
        data, [](void* p) noexcept { delete[] static_cast<int32_t*>(p); });

    return nb::ndarray<nb::numpy, int32_t, nb::ndim<2>>(data, 2, shape, owner);
  }

  // uint8 フォーマット用の read (shape: [frames, channels])
  nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>> read_uint8(
      unsigned long frames) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    size_t total_samples = frames * input_channels_;
    uint8_t* data = new uint8_t[total_samples];

    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_ReadStream(stream_, data, frames);
    }
    if (err != paNoError && err != paInputOverflowed) {
      delete[] data;
      check_pa_error(err, "Failed to read stream");
    }

    size_t shape[2] = {frames, static_cast<size_t>(input_channels_)};
    nb::capsule owner(
        data, [](void* p) noexcept { delete[] static_cast<uint8_t*>(p); });

    return nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>>(data, 2, shape, owner);
  }

  // float32 フォーマット用の write (shape: [frames, channels])
  void write_float32(nb::ndarray<nb::numpy, const float, nb::ndim<2>> buffer) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    unsigned long frames = buffer.shape(0);
    const void* data = buffer.data();
    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_WriteStream(stream_, data, frames);
    }
    if (err != paNoError && err != paOutputUnderflowed) {
      check_pa_error(err, "Failed to write stream");
    }
  }

  // int16 フォーマット用の write (shape: [frames, channels])
  void write_int16(nb::ndarray<nb::numpy, const int16_t, nb::ndim<2>> buffer) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    unsigned long frames = buffer.shape(0);
    const void* data = buffer.data();
    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_WriteStream(stream_, data, frames);
    }
    if (err != paNoError && err != paOutputUnderflowed) {
      check_pa_error(err, "Failed to write stream");
    }
  }

  // int32 フォーマット用の write (shape: [frames, channels])
  void write_int32(nb::ndarray<nb::numpy, const int32_t, nb::ndim<2>> buffer) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    unsigned long frames = buffer.shape(0);
    const void* data = buffer.data();
    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_WriteStream(stream_, data, frames);
    }
    if (err != paNoError && err != paOutputUnderflowed) {
      check_pa_error(err, "Failed to write stream");
    }
  }

  // uint8 フォーマット用の write (shape: [frames, channels])
  void write_uint8(nb::ndarray<nb::numpy, const uint8_t, nb::ndim<2>> buffer) {
    if (!stream_) {
      throw std::runtime_error("Stream is not open");
    }

    unsigned long frames = buffer.shape(0);
    const void* data = buffer.data();
    PaError err;
    {
      nb::gil_scoped_release release;
      err = Pa_WriteStream(stream_, data, frames);
    }
    if (err != paNoError && err != paOutputUnderflowed) {
      check_pa_error(err, "Failed to write stream");
    }
  }

  double sample_rate() const { return sample_rate_; }
  int input_channels() const { return input_channels_; }
  int output_channels() const { return output_channels_; }
  SampleFormat format() const {
    PaSampleFormat fmt = (input_channels_ > 0) ? input_format_ : output_format_;
    switch (fmt) {
      case paFloat32:
        return SampleFormat::FLOAT32;
      case paInt32:
        return SampleFormat::INT32;
      case paInt24:
        return SampleFormat::INT24;
      case paInt16:
        return SampleFormat::INT16;
      case paInt8:
        return SampleFormat::INT8;
      case paUInt8:
        return SampleFormat::UINT8;
      default:
        return SampleFormat::FLOAT32;
    }
  }

 private:
  PaStream* stream_;
  bool is_active_;
  double sample_rate_ = 0.0;
  int input_channels_ = 0;
  int output_channels_ = 0;
  PaSampleFormat input_format_ = paFloat32;
  PaSampleFormat output_format_ = paFloat32;
};

// ========== ヘルパー関数 ==========

// デバイスインデックスを取得 (int または DeviceInfoWrapper から)
static PaDeviceIndex get_device_index(const nb::object& device) {
  if (nb::isinstance<nb::int_>(device)) {
    return nb::cast<PaDeviceIndex>(device);
  } else if (nb::isinstance<DeviceInfoWrapper>(device)) {
    return nb::cast<DeviceInfoWrapper>(device).index;
  } else {
    throw std::runtime_error("device must be an int or DeviceInfo");
  }
}

// ========== 型バインディング ==========
static void init_pa_types(nb::module_& m) {
  // SampleFormat enum
  nb::enum_<SampleFormat>(m, "SampleFormat")
      .value("FLOAT32", SampleFormat::FLOAT32)
      .value("INT32", SampleFormat::INT32)
      .value("INT24", SampleFormat::INT24)
      .value("INT16", SampleFormat::INT16)
      .value("INT8", SampleFormat::INT8)
      .value("UINT8", SampleFormat::UINT8);

  // エラーコード
  nb::enum_<PaErrorCode>(m, "ErrorCode")
      .value("NoError", paNoError)
      .value("NotInitialized", paNotInitialized)
      .value("UnanticipatedHostError", paUnanticipatedHostError)
      .value("InvalidChannelCount", paInvalidChannelCount)
      .value("InvalidSampleRate", paInvalidSampleRate)
      .value("InvalidDevice", paInvalidDevice)
      .value("InvalidFlag", paInvalidFlag)
      .value("SampleFormatNotSupported", paSampleFormatNotSupported)
      .value("BadIODeviceCombination", paBadIODeviceCombination)
      .value("InsufficientMemory", paInsufficientMemory)
      .value("BufferTooBig", paBufferTooBig)
      .value("BufferTooSmall", paBufferTooSmall)
      .value("NullCallback", paNullCallback)
      .value("BadStreamPtr", paBadStreamPtr)
      .value("TimedOut", paTimedOut)
      .value("InternalError", paInternalError)
      .value("DeviceUnavailable", paDeviceUnavailable)
      .value("IncompatibleHostApiSpecificStreamInfo",
             paIncompatibleHostApiSpecificStreamInfo)
      .value("StreamIsStopped", paStreamIsStopped)
      .value("StreamIsNotStopped", paStreamIsNotStopped)
      .value("InputOverflowed", paInputOverflowed)
      .value("OutputUnderflowed", paOutputUnderflowed)
      .value("HostApiNotFound", paHostApiNotFound)
      .value("InvalidHostApi", paInvalidHostApi)
      .value("CanNotReadFromACallbackStream", paCanNotReadFromACallbackStream)
      .value("CanNotWriteToACallbackStream", paCanNotWriteToACallbackStream)
      .value("CanNotReadFromAnOutputOnlyStream",
             paCanNotReadFromAnOutputOnlyStream)
      .value("CanNotWriteToAnInputOnlyStream", paCanNotWriteToAnInputOnlyStream)
      .value("IncompatibleStreamHostApi", paIncompatibleStreamHostApi)
      .value("BadBufferPtr", paBadBufferPtr)
      .value("CanNotInitializeRecursively", paCanNotInitializeRecursively)
      .export_values();

  // Host API Type ID
  nb::enum_<PaHostApiTypeId>(m, "HostApiTypeId")
      .value("InDevelopment", paInDevelopment)
      .value("DirectSound", paDirectSound)
      .value("MME", paMME)
      .value("ASIO", paASIO)
      .value("SoundManager", paSoundManager)
      .value("CoreAudio", paCoreAudio)
      .value("OSS", paOSS)
      .value("ALSA", paALSA)
      .value("AL", paAL)
      .value("BeOS", paBeOS)
      .value("WDMKS", paWDMKS)
      .value("JACK", paJACK)
      .value("WASAPI", paWASAPI)
      .value("AudioScienceHPI", paAudioScienceHPI)
      .value("AudioIO", paAudioIO)
      .value("PulseAudio", paPulseAudio)
      .value("Sndio", paSndio)
      .export_values();

  // Stream Callback Result
  nb::enum_<PaStreamCallbackResult>(m, "StreamCallbackResult")
      .value("Continue", paContinue)
      .value("Complete", paComplete)
      .value("Abort", paAbort)
      .export_values();

  // 後方互換性のための定数 (低レベル API)
  m.attr("FLOAT32") = nb::int_(paFloat32);
  m.attr("INT32") = nb::int_(paInt32);
  m.attr("INT24") = nb::int_(paInt24);
  m.attr("INT16") = nb::int_(paInt16);
  m.attr("INT8") = nb::int_(paInt8);
  m.attr("UINT8") = nb::int_(paUInt8);
  m.attr("CUSTOM_FORMAT") = nb::int_(paCustomFormat);
  m.attr("NON_INTERLEAVED") = nb::int_(paNonInterleaved);

  // Stream Flags (定数として公開)
  m.attr("NO_FLAG") = nb::int_(paNoFlag);
  m.attr("CLIP_OFF") = nb::int_(paClipOff);
  m.attr("DITHER_OFF") = nb::int_(paDitherOff);
  m.attr("NEVER_DROP_INPUT") = nb::int_(paNeverDropInput);
  m.attr("PRIME_OUTPUT_BUFFERS_USING_STREAM_CALLBACK") =
      nb::int_(paPrimeOutputBuffersUsingStreamCallback);

  // Stream Callback Flags (定数として公開)
  m.attr("INPUT_UNDERFLOW") = nb::int_(paInputUnderflow);
  m.attr("INPUT_OVERFLOW") = nb::int_(paInputOverflow);
  m.attr("OUTPUT_UNDERFLOW") = nb::int_(paOutputUnderflow);
  m.attr("OUTPUT_OVERFLOW") = nb::int_(paOutputOverflow);
  m.attr("PRIMING_OUTPUT") = nb::int_(paPrimingOutput);

  // 特殊な定数
  m.attr("NO_DEVICE") = nb::int_(paNoDevice);
  m.attr("FRAMES_PER_BUFFER_UNSPECIFIED") =
      nb::int_(paFramesPerBufferUnspecified);

  // DeviceInfo (新しいラッパー)
  nb::class_<DeviceInfoWrapper>(m, "DeviceInfo")
      .def_ro("index", &DeviceInfoWrapper::index)
      .def_ro("name", &DeviceInfoWrapper::name)
      .def_ro("host_api", &DeviceInfoWrapper::host_api)
      .def_ro("max_input_channels", &DeviceInfoWrapper::max_input_channels)
      .def_ro("max_output_channels", &DeviceInfoWrapper::max_output_channels)
      .def_ro("default_low_input_latency",
              &DeviceInfoWrapper::default_low_input_latency)
      .def_ro("default_low_output_latency",
              &DeviceInfoWrapper::default_low_output_latency)
      .def_ro("default_high_input_latency",
              &DeviceInfoWrapper::default_high_input_latency)
      .def_ro("default_high_output_latency",
              &DeviceInfoWrapper::default_high_output_latency)
      .def_ro("default_sample_rate", &DeviceInfoWrapper::default_sample_rate)
      .def("__repr__", [](const DeviceInfoWrapper& info) {
        return "DeviceInfo(index=" + std::to_string(info.index) + ", name='" +
               info.name + "')";
      });

  // Version Info 構造体
  nb::class_<PaVersionInfo>(m, "VersionInfo")
      .def_ro("version_major", &PaVersionInfo::versionMajor)
      .def_ro("version_minor", &PaVersionInfo::versionMinor)
      .def_ro("version_sub_minor", &PaVersionInfo::versionSubMinor)
      .def_prop_ro("version_control_revision",
                   [](const PaVersionInfo& info) -> std::optional<std::string> {
                     if (info.versionControlRevision) {
                       return std::string(info.versionControlRevision);
                     }
                     return std::nullopt;
                   })
      .def_prop_ro("version_text",
                   [](const PaVersionInfo& info) -> std::optional<std::string> {
                     if (info.versionText) {
                       return std::string(info.versionText);
                     }
                     return std::nullopt;
                   });

  // Host API Info 構造体
  nb::class_<PaHostApiInfo>(m, "HostApiInfo")
      .def_ro("struct_version", &PaHostApiInfo::structVersion)
      .def_ro("type", &PaHostApiInfo::type)
      .def_prop_ro("name",
                   [](const PaHostApiInfo& info) -> std::optional<std::string> {
                     if (info.name) {
                       return std::string(info.name);
                     }
                     return std::nullopt;
                   })
      .def_ro("device_count", &PaHostApiInfo::deviceCount)
      .def_ro("default_input_device", &PaHostApiInfo::defaultInputDevice)
      .def_ro("default_output_device", &PaHostApiInfo::defaultOutputDevice);

  // Stream Info 構造体
  nb::class_<PaStreamInfo>(m, "StreamInfo")
      .def_ro("struct_version", &PaStreamInfo::structVersion)
      .def_ro("input_latency", &PaStreamInfo::inputLatency)
      .def_ro("output_latency", &PaStreamInfo::outputLatency)
      .def_ro("sample_rate", &PaStreamInfo::sampleRate);

  // Host Error Info 構造体
  nb::class_<PaHostErrorInfo>(m, "HostErrorInfo")
      .def_ro("host_api_type", &PaHostErrorInfo::hostApiType)
      .def_ro("error_code", &PaHostErrorInfo::errorCode)
      .def_prop_ro(
          "error_text",
          [](const PaHostErrorInfo& info) -> std::optional<std::string> {
            if (info.errorText && info.errorText[0] != '\0') {
              return std::string(info.errorText);
            }
            return std::nullopt;
          });

  // Stream Callback Time Info 構造体
  nb::class_<PaStreamCallbackTimeInfo>(m, "StreamCallbackTimeInfo")
      .def_ro("input_buffer_adc_time",
              &PaStreamCallbackTimeInfo::inputBufferAdcTime)
      .def_ro("current_time", &PaStreamCallbackTimeInfo::currentTime)
      .def_ro("output_buffer_dac_time",
              &PaStreamCallbackTimeInfo::outputBufferDacTime);
}

// ========== Stream バインディング ==========
static void init_pa_stream(nb::module_& m) {
  // StreamParameters クラス (低レベル API)
  nb::class_<StreamParameters>(m, "StreamParameters")
      .def(nb::init<PaDeviceIndex, int, PaSampleFormat, PaTime>(), "device"_a,
           "channel_count"_a, "sample_format"_a = paFloat32,
           "suggested_latency"_a = 0.0)
      .def_rw("device", &StreamParameters::device)
      .def_rw("channel_count", &StreamParameters::channel_count)
      .def_rw("sample_format", &StreamParameters::sample_format)
      .def_rw("suggested_latency", &StreamParameters::suggested_latency);

  // Stream クラス
  nb::class_<Stream>(m, "Stream")
      .def(nb::init<std::optional<StreamParameters>,
                    std::optional<StreamParameters>, double, unsigned long,
                    PaStreamFlags>(),
           "input_parameters"_a = nb::none(),
           "output_parameters"_a = nb::none(), "sample_rate"_a = 44100.0,
           "frames_per_buffer"_a = paFramesPerBufferUnspecified,
           "stream_flags"_a = paNoFlag)
      .def("start", &Stream::start)
      .def("stop", &Stream::stop)
      .def("abort", &Stream::abort)
      .def("close", &Stream::close)
      .def("is_stopped", &Stream::is_stopped)
      .def("is_active", &Stream::is_active)
      .def("get_time", &Stream::get_time)
      .def("get_cpu_load", &Stream::get_cpu_load)
      .def("get_info", &Stream::get_info)
      .def("get_read_available", &Stream::get_read_available)
      .def("get_write_available", &Stream::get_write_available)
      .def("read", &Stream::read, "frames"_a)
      .def("write", &Stream::write, "buffer"_a)
      .def("read_float32", &Stream::read_float32, "frames"_a)
      .def("read_int16", &Stream::read_int16, "frames"_a)
      .def("read_int32", &Stream::read_int32, "frames"_a)
      .def("read_uint8", &Stream::read_uint8, "frames"_a)
      .def("write_float32", &Stream::write_float32, "buffer"_a)
      .def("write_int16", &Stream::write_int16, "buffer"_a)
      .def("write_int32", &Stream::write_int32, "buffer"_a)
      .def("write_uint8", &Stream::write_uint8, "buffer"_a)
      .def_prop_ro("sample_rate", &Stream::sample_rate)
      .def_prop_ro("input_channels", &Stream::input_channels)
      .def_prop_ro("output_channels", &Stream::output_channels)
      .def_prop_ro("format", &Stream::format)
      .def(
          "__enter__",
          [](Stream& self) -> Stream& { return self.start(), self; },
          nb::sig("def __enter__(self) -> Stream"))
      .def(
          "__exit__",
          [](Stream& self, nb::object, nb::object, nb::object) {
            self.close();
          },
          nb::arg().none(), nb::arg().none(), nb::arg().none(),
          nb::sig("def __exit__(self, exc_type: type[BaseException] | None, "
                  "exc_val: BaseException | None, "
                  "exc_tb: types.TracebackType | None) -> None"));

  // is_format_supported 関数 (低レベル API)
  m.def(
      "is_format_supported",
      [](std::optional<StreamParameters> input_params,
         std::optional<StreamParameters> output_params,
         double sample_rate) -> bool {
        const PaStreamParameters* input_ptr = nullptr;
        const PaStreamParameters* output_ptr = nullptr;

        PaStreamParameters input_pa, output_pa;

        if (input_params.has_value()) {
          input_pa = input_params->to_pa();
          input_ptr = &input_pa;
        }

        if (output_params.has_value()) {
          output_pa = output_params->to_pa();
          output_ptr = &output_pa;
        }

        PaError result =
            Pa_IsFormatSupported(input_ptr, output_ptr, sample_rate);
        return result == paFormatIsSupported;
      },
      "input_parameters"_a = nb::none(), "output_parameters"_a = nb::none(),
      "sample_rate"_a = 44100.0);
}

// ========== モジュール定義 ==========
NB_MODULE(portaudio_ext, m) {
  m.doc() = "PortAudio bindings for Python";

  // 型バインディングを初期化
  init_pa_types(m);

  // Stream クラスを初期化
  init_pa_stream(m);

  // ========== 高レベル API ==========

  // list_devices
  m.def("list_devices", []() -> std::vector<DeviceInfoWrapper> {
    ensure_pa_init();
    std::vector<DeviceInfoWrapper> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; i++) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (info) {
        devices.push_back(DeviceInfoWrapper::from_pa(i, info));
      }
    }
    return devices;
  });

  // list_input_devices
  m.def("list_input_devices", []() -> std::vector<DeviceInfoWrapper> {
    ensure_pa_init();
    std::vector<DeviceInfoWrapper> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; i++) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (info && info->maxInputChannels > 0) {
        devices.push_back(DeviceInfoWrapper::from_pa(i, info));
      }
    }
    return devices;
  });

  // list_output_devices
  m.def("list_output_devices", []() -> std::vector<DeviceInfoWrapper> {
    ensure_pa_init();
    std::vector<DeviceInfoWrapper> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; i++) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (info && info->maxOutputChannels > 0) {
        devices.push_back(DeviceInfoWrapper::from_pa(i, info));
      }
    }
    return devices;
  });

  // open_input
  m.def(
      "open_input",
      [](nb::object device, double sample_rate, int channels,
         SampleFormat format, unsigned long frames_per_buffer) -> Stream* {
        ensure_pa_init();
        PaDeviceIndex device_index;

        if (device.is_none()) {
          device_index = Pa_GetDefaultInputDevice();
          if (device_index == paNoDevice) {
            throw std::runtime_error("No default input device available");
          }
        } else {
          device_index = get_device_index(device);
        }

        const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
        if (!info) {
          throw std::runtime_error("Invalid device index");
        }

        StreamParameters params(device_index, channels, to_pa_format(format),
                                info->defaultLowInputLatency);

        return new Stream(params, std::nullopt, sample_rate, frames_per_buffer,
                          paNoFlag);
      },
      "device"_a = nb::none(), "sample_rate"_a = 44100.0, "channels"_a = 1,
      "format"_a = SampleFormat::FLOAT32, "frames_per_buffer"_a = 1024,
      nb::rv_policy::take_ownership);

  // open_output
  m.def(
      "open_output",
      [](nb::object device, double sample_rate, int channels,
         SampleFormat format, unsigned long frames_per_buffer) -> Stream* {
        ensure_pa_init();
        PaDeviceIndex device_index;

        if (device.is_none()) {
          device_index = Pa_GetDefaultOutputDevice();
          if (device_index == paNoDevice) {
            throw std::runtime_error("No default output device available");
          }
        } else {
          device_index = get_device_index(device);
        }

        const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
        if (!info) {
          throw std::runtime_error("Invalid device index");
        }

        StreamParameters params(device_index, channels, to_pa_format(format),
                                info->defaultLowOutputLatency);

        return new Stream(std::nullopt, params, sample_rate, frames_per_buffer,
                          paNoFlag);
      },
      "device"_a = nb::none(), "sample_rate"_a = 44100.0, "channels"_a = 1,
      "format"_a = SampleFormat::FLOAT32, "frames_per_buffer"_a = 1024,
      nb::rv_policy::take_ownership);

  // ========== 低レベル API ==========

  // バージョン関数
  m.def("get_version", []() { return Pa_GetVersion(); });

  m.def("get_version_text", []() -> std::string {
    const char* text = Pa_GetVersionText();
    return text ? std::string(text) : "";
  });

  m.def(
      "get_version_info",
      []() -> const PaVersionInfo* { return Pa_GetVersionInfo(); },
      nb::rv_policy::reference);

  // エラー関数
  m.def(
      "get_error_text",
      [](PaError error_code) -> std::string {
        const char* text = Pa_GetErrorText(error_code);
        return text ? std::string(text) : "";
      },
      "error_code"_a);

  m.def(
      "get_last_host_error_info",
      []() -> const PaHostErrorInfo* {
        ensure_pa_init();
        return Pa_GetLastHostErrorInfo();
      },
      nb::rv_policy::reference);

  // Host API 関数
  m.def("get_host_api_count", []() {
    ensure_pa_init();
    return Pa_GetHostApiCount();
  });

  m.def("get_default_host_api", []() {
    ensure_pa_init();
    return Pa_GetDefaultHostApi();
  });

  m.def(
      "get_host_api_info",
      [](PaHostApiIndex host_api) -> const PaHostApiInfo* {
        ensure_pa_init();
        return Pa_GetHostApiInfo(host_api);
      },
      "host_api"_a, nb::rv_policy::reference);

  m.def(
      "host_api_type_id_to_host_api_index",
      [](PaHostApiTypeId type) {
        ensure_pa_init();
        return Pa_HostApiTypeIdToHostApiIndex(type);
      },
      "type"_a);

  m.def(
      "host_api_device_index_to_device_index",
      [](PaHostApiIndex host_api, int host_api_device_index) {
        ensure_pa_init();
        return Pa_HostApiDeviceIndexToDeviceIndex(host_api,
                                                  host_api_device_index);
      },
      "host_api"_a, "host_api_device_index"_a);

  // デバイス関数
  m.def("get_device_count", []() {
    ensure_pa_init();
    return Pa_GetDeviceCount();
  });

  m.def("get_default_input_device", []() {
    ensure_pa_init();
    return Pa_GetDefaultInputDevice();
  });

  m.def("get_default_output_device", []() {
    ensure_pa_init();
    return Pa_GetDefaultOutputDevice();
  });

  m.def(
      "get_device_info",
      [](PaDeviceIndex device) -> std::optional<DeviceInfoWrapper> {
        ensure_pa_init();
        const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
        if (info) {
          return DeviceInfoWrapper::from_pa(device, info);
        }
        return std::nullopt;
      },
      "device"_a);

  // ユーティリティ関数
  m.def(
      "get_sample_size",
      [](PaSampleFormat format) {
        ensure_pa_init();
        return Pa_GetSampleSize(format);
      },
      "format"_a);

  m.def("sleep", [](long msec) { Pa_Sleep(msec); }, "msec"_a);

  // 後方互換性のためのヘルパー関数
  m.def("get_all_devices",
        []() -> std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> {
          ensure_pa_init();
          std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> devices;
          int count = Pa_GetDeviceCount();
          for (int i = 0; i < count; i++) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info) {
              devices.push_back({i, DeviceInfoWrapper::from_pa(i, info)});
            }
          }
          return devices;
        });

  m.def("get_input_devices",
        []() -> std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> {
          ensure_pa_init();
          std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> devices;
          int count = Pa_GetDeviceCount();
          for (int i = 0; i < count; i++) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxInputChannels > 0) {
              devices.push_back({i, DeviceInfoWrapper::from_pa(i, info)});
            }
          }
          return devices;
        });

  m.def("get_output_devices",
        []() -> std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> {
          ensure_pa_init();
          std::vector<std::pair<PaDeviceIndex, DeviceInfoWrapper>> devices;
          int count = Pa_GetDeviceCount();
          for (int i = 0; i < count; i++) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxOutputChannels > 0) {
              devices.push_back({i, DeviceInfoWrapper::from_pa(i, info)});
            }
          }
          return devices;
        });

  m.def(
      "get_all_host_apis",
      []() -> std::vector<std::pair<PaHostApiIndex, const PaHostApiInfo*>> {
        ensure_pa_init();
        std::vector<std::pair<PaHostApiIndex, const PaHostApiInfo*>> apis;
        int count = Pa_GetHostApiCount();
        for (int i = 0; i < count; i++) {
          const PaHostApiInfo* info = Pa_GetHostApiInfo(i);
          if (info) {
            apis.push_back({i, info});
          }
        }
        return apis;
      },
      nb::rv_policy::reference);
}

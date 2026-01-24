"""PortAudio のテスト"""

import numpy as np
import pytest

import portaudio as pa


def has_audio_device() -> bool:
    """オーディオデバイスがあるかどうかを確認する"""
    return len(pa.list_devices()) > 0


def has_input_device() -> bool:
    """入力デバイスがあるかどうかを確認する"""
    return pa.get_default_input_device() != pa.NO_DEVICE


def has_output_device() -> bool:
    """出力デバイスがあるかどうかを確認する"""
    return pa.get_default_output_device() != pa.NO_DEVICE


# オーディオデバイスがない場合はモジュール全体をスキップ
pytestmark = pytest.mark.skipif(
    not has_audio_device(),
    reason="オーディオデバイスがありません",
)

requires_input_device = pytest.mark.skipif(
    not has_input_device(),
    reason="入力デバイスがありません",
)

requires_output_device = pytest.mark.skipif(
    not has_output_device(),
    reason="出力デバイスがありません",
)


# ========== 基本機能テスト ==========


def test_get_version() -> None:
    """バージョン番号を取得できることを確認する"""
    version = pa.get_version()
    assert isinstance(version, int)
    assert version > 0


def test_get_version_text() -> None:
    """バージョンテキストを取得できることを確認する"""
    text = pa.get_version_text()
    assert isinstance(text, str)
    assert len(text) > 0
    assert "PortAudio" in text


def test_get_version_info() -> None:
    """バージョン情報を取得できることを確認する"""
    info = pa.get_version_info()
    assert info is not None
    assert info.version_major >= 0
    assert info.version_minor >= 0
    assert info.version_sub_minor >= 0


def test_get_host_api_count() -> None:
    """ホスト API の数を取得できることを確認する"""
    count = pa.get_host_api_count()
    assert isinstance(count, int)
    assert count >= 1


def test_get_default_host_api() -> None:
    """デフォルトホスト API を取得できることを確認する"""
    index = pa.get_default_host_api()
    assert isinstance(index, int)
    assert index >= 0


def test_get_host_api_info() -> None:
    """ホスト API 情報を取得できることを確認する"""
    index = pa.get_default_host_api()
    info = pa.get_host_api_info(index)
    assert info is not None
    assert info.name is not None
    assert len(info.name) > 0


def test_get_all_host_apis() -> None:
    """全てのホスト API を取得できることを確認する"""
    apis = pa.get_all_host_apis()
    assert isinstance(apis, list)
    assert len(apis) >= 1
    for index, info in apis:
        assert isinstance(index, int)
        assert info is not None


def test_get_device_count() -> None:
    """デバイス数を取得できることを確認する"""
    count = pa.get_device_count()
    assert isinstance(count, int)
    assert count >= 0


def test_get_sample_size() -> None:
    """サンプルサイズを取得できることを確認する"""
    assert pa.get_sample_size(pa.FLOAT32) == 4
    assert pa.get_sample_size(pa.INT32) == 4
    assert pa.get_sample_size(pa.INT16) == 2
    assert pa.get_sample_size(pa.INT8) == 1
    assert pa.get_sample_size(pa.UINT8) == 1


def test_get_error_text() -> None:
    """エラーテキストを取得できることを確認する"""
    text = pa.get_error_text(0)
    assert isinstance(text, str)


def test_sleep() -> None:
    """sleep 関数が動作することを確認する"""
    pa.sleep(1)


def test_sample_format_enum() -> None:
    """SampleFormat enum が存在することを確認する"""
    assert pa.SampleFormat.FLOAT32 is not None
    assert pa.SampleFormat.INT32 is not None
    assert pa.SampleFormat.INT16 is not None
    assert pa.SampleFormat.UINT8 is not None


# ========== 高レベル API テスト ==========


def test_list_devices() -> None:
    """全てのデバイスを取得できることを確認する"""
    devices = pa.list_devices()
    assert isinstance(devices, list)
    for device in devices:
        assert hasattr(device, "index")
        assert hasattr(device, "name")
        assert hasattr(device, "max_input_channels")
        assert hasattr(device, "max_output_channels")


@requires_input_device
def test_list_input_devices() -> None:
    """入力デバイス一覧を取得できることを確認する"""
    devices = pa.list_input_devices()
    assert isinstance(devices, list)
    assert len(devices) >= 1
    for device in devices:
        assert device.max_input_channels > 0


@requires_output_device
def test_list_output_devices() -> None:
    """出力デバイス一覧を取得できることを確認する"""
    devices = pa.list_output_devices()
    assert isinstance(devices, list)
    assert len(devices) >= 1
    for device in devices:
        assert device.max_output_channels > 0


@requires_input_device
def test_open_input_default() -> None:
    """デフォルト入力デバイスでストリームを開けることを確認する"""
    stream = pa.open_input()
    assert stream is not None
    assert stream.is_stopped()
    stream.close()


@requires_output_device
def test_open_output_default() -> None:
    """デフォルト出力デバイスでストリームを開けることを確認する"""
    stream = pa.open_output()
    assert stream is not None
    assert stream.is_stopped()
    stream.close()


@requires_input_device
def test_open_input_with_device_info() -> None:
    """DeviceInfo を使って入力ストリームを開けることを確認する"""
    devices = pa.list_input_devices()
    assert len(devices) >= 1
    stream = pa.open_input(device=devices[0])
    assert stream is not None
    stream.close()


@requires_output_device
def test_open_output_with_device_info() -> None:
    """DeviceInfo を使って出力ストリームを開けることを確認する"""
    devices = pa.list_output_devices()
    assert len(devices) >= 1
    stream = pa.open_output(device=devices[0])
    assert stream is not None
    stream.close()


@requires_input_device
def test_open_input_with_format() -> None:
    """フォーマット指定で入力ストリームを開けることを確認する"""
    stream = pa.open_input(format=pa.SampleFormat.INT16)
    assert stream is not None
    assert stream.format == pa.SampleFormat.INT16
    stream.close()


@requires_output_device
def test_open_output_with_format() -> None:
    """フォーマット指定で出力ストリームを開けることを確認する"""
    stream = pa.open_output(format=pa.SampleFormat.INT16)
    assert stream is not None
    assert stream.format == pa.SampleFormat.INT16
    stream.close()


@requires_input_device
def test_stream_read() -> None:
    """統一 read メソッドが動作することを確認する"""
    with pa.open_input() as stream:
        data = stream.read(1024)
        assert data.shape == (1024, 1)
        assert data.dtype == np.float32


@requires_input_device
def test_stream_read_int16() -> None:
    """INT16 フォーマットで read が動作することを確認する"""
    with pa.open_input(format=pa.SampleFormat.INT16) as stream:
        data = stream.read(1024)
        assert data.shape == (1024, 1)
        assert data.dtype == np.int16


@requires_output_device
def test_stream_write() -> None:
    """統一 write メソッドが動作することを確認する"""
    with pa.open_output() as stream:
        data = np.zeros((1024, 1), dtype=np.float32)
        stream.write(data)


@requires_output_device
def test_stream_write_int16() -> None:
    """INT16 フォーマットで write が動作することを確認する"""
    with pa.open_output(format=pa.SampleFormat.INT16) as stream:
        data = np.zeros((1024, 1), dtype=np.int16)
        stream.write(data)


@requires_input_device
def test_stream_context_manager() -> None:
    """コンテキストマネージャが動作することを確認する"""
    with pa.open_input() as stream:
        assert stream.is_active()
    # コンテキスト終了後は close されている


# ========== 低レベル API テスト ==========


@requires_input_device
def test_get_default_input_device() -> None:
    """デフォルト入力デバイスを取得できることを確認する"""
    device_index = pa.get_default_input_device()
    assert device_index != pa.NO_DEVICE
    info = pa.get_device_info(device_index)
    assert info is not None
    assert info.max_input_channels > 0


@requires_output_device
def test_get_default_output_device() -> None:
    """デフォルト出力デバイスを取得できることを確認する"""
    device_index = pa.get_default_output_device()
    assert device_index != pa.NO_DEVICE
    info = pa.get_device_info(device_index)
    assert info is not None
    assert info.max_output_channels > 0


@requires_input_device
def test_get_input_devices() -> None:
    """入力デバイス一覧を取得できることを確認する (低レベル API)"""
    devices = pa.get_input_devices()
    assert isinstance(devices, list)
    assert len(devices) >= 1
    for index, info in devices:
        assert info.max_input_channels > 0


@requires_output_device
def test_get_output_devices() -> None:
    """出力デバイス一覧を取得できることを確認する (低レベル API)"""
    devices = pa.get_output_devices()
    assert isinstance(devices, list)
    assert len(devices) >= 1
    for index, info in devices:
        assert info.max_output_channels > 0


@requires_input_device
def test_is_format_supported_input() -> None:
    """入力フォーマットがサポートされているか確認する"""
    device_index = pa.get_default_input_device()
    info = pa.get_device_info(device_index)
    assert info is not None

    params = pa.StreamParameters(
        device=device_index,
        channel_count=1,
        sample_format=pa.FLOAT32,
        suggested_latency=info.default_low_input_latency,
    )
    supported = pa.is_format_supported(
        input_parameters=params,
        sample_rate=info.default_sample_rate,
    )
    assert isinstance(supported, bool)


@requires_output_device
def test_is_format_supported_output() -> None:
    """出力フォーマットがサポートされているか確認する"""
    device_index = pa.get_default_output_device()
    info = pa.get_device_info(device_index)
    assert info is not None

    params = pa.StreamParameters(
        device=device_index,
        channel_count=1,
        sample_format=pa.FLOAT32,
        suggested_latency=info.default_low_output_latency,
    )
    supported = pa.is_format_supported(
        output_parameters=params,
        sample_rate=info.default_sample_rate,
    )
    assert isinstance(supported, bool)


@requires_input_device
def test_stream_low_level_input() -> None:
    """低レベル API で入力ストリームを開閉できることを確認する"""
    device_index = pa.get_default_input_device()
    info = pa.get_device_info(device_index)
    assert info is not None

    params = pa.StreamParameters(
        device=device_index,
        channel_count=1,
        sample_format=pa.FLOAT32,
        suggested_latency=info.default_low_input_latency,
    )
    stream = pa.Stream(
        input_parameters=params,
        sample_rate=info.default_sample_rate,
        frames_per_buffer=1024,
    )
    assert stream.is_stopped()
    assert not stream.is_active()
    stream.close()


@requires_output_device
def test_stream_low_level_output() -> None:
    """低レベル API で出力ストリームを開閉できることを確認する"""
    device_index = pa.get_default_output_device()
    info = pa.get_device_info(device_index)
    assert info is not None

    params = pa.StreamParameters(
        device=device_index,
        channel_count=1,
        sample_format=pa.FLOAT32,
        suggested_latency=info.default_low_output_latency,
    )
    stream = pa.Stream(
        output_parameters=params,
        sample_rate=info.default_sample_rate,
        frames_per_buffer=1024,
    )
    assert stream.is_stopped()
    assert not stream.is_active()
    stream.close()


@requires_input_device
def test_stream_get_info() -> None:
    """ストリーム情報を取得できることを確認する"""
    with pa.open_input() as stream:
        stream_info = stream.get_info()
        assert stream_info is not None
        assert stream_info.sample_rate > 0


@requires_input_device
def test_stream_get_time() -> None:
    """ストリーム時間を取得できることを確認する"""
    with pa.open_input() as stream:
        time = stream.get_time()
        assert isinstance(time, float)
        assert time >= 0


@requires_input_device
def test_stream_get_cpu_load() -> None:
    """CPU 負荷を取得できることを確認する"""
    with pa.open_input() as stream:
        cpu_load = stream.get_cpu_load()
        assert isinstance(cpu_load, float)
        assert cpu_load >= 0


@requires_input_device
def test_stream_properties() -> None:
    """ストリームのプロパティを取得できることを確認する"""
    stream = pa.open_input(channels=1, sample_rate=44100.0)
    assert stream.sample_rate == 44100.0
    assert stream.input_channels == 1
    assert stream.output_channels == 0
    assert stream.format == pa.SampleFormat.FLOAT32
    stream.close()


@requires_input_device
def test_stream_abort() -> None:
    """ストリームを中断できることを確認する"""
    stream = pa.open_input()
    stream.start()
    assert stream.is_active()
    stream.abort()
    assert stream.is_stopped()
    stream.close()


@requires_input_device
def test_stream_read_available() -> None:
    """読み込み可能なフレーム数を取得できることを確認する"""
    with pa.open_input() as stream:
        pa.sleep(100)
        available = stream.get_read_available()
        assert isinstance(available, int)
        assert available >= 0


@requires_output_device
def test_stream_write_available() -> None:
    """書き込み可能なフレーム数を取得できることを確認する"""
    with pa.open_output() as stream:
        available = stream.get_write_available()
        assert isinstance(available, int)
        assert available >= 0


# ========== フォーマット不一致テスト ==========


@requires_input_device
def test_read_format_mismatch_int16() -> None:
    """FLOAT32 ストリームで read_int16 を呼ぶと例外が発生することを確認する"""
    with pa.open_input(format=pa.SampleFormat.FLOAT32) as stream:
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.read_int16(1024)


@requires_input_device
def test_read_format_mismatch_int32() -> None:
    """FLOAT32 ストリームで read_int32 を呼ぶと例外が発生することを確認する"""
    with pa.open_input(format=pa.SampleFormat.FLOAT32) as stream:
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.read_int32(1024)


@requires_input_device
def test_read_format_mismatch_uint8() -> None:
    """FLOAT32 ストリームで read_uint8 を呼ぶと例外が発生することを確認する"""
    with pa.open_input(format=pa.SampleFormat.FLOAT32) as stream:
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.read_uint8(1024)


@requires_input_device
def test_read_format_mismatch_float32() -> None:
    """INT16 ストリームで read_float32 を呼ぶと例外が発生することを確認する"""
    with pa.open_input(format=pa.SampleFormat.INT16) as stream:
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.read_float32(1024)


@requires_output_device
def test_write_format_mismatch_int16() -> None:
    """FLOAT32 ストリームで write_int16 を呼ぶと例外が発生することを確認する"""
    with pa.open_output(format=pa.SampleFormat.FLOAT32) as stream:
        data = np.zeros((1024, 1), dtype=np.int16)
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.write_int16(data)


@requires_output_device
def test_write_format_mismatch_float32() -> None:
    """INT16 ストリームで write_float32 を呼ぶと例外が発生することを確認する"""
    with pa.open_output(format=pa.SampleFormat.INT16) as stream:
        data = np.zeros((1024, 1), dtype=np.float32)
        with pytest.raises(RuntimeError, match="format mismatch"):
            stream.write_float32(data)


# ========== write 検証テスト ==========


@requires_output_device
def test_write_dtype_mismatch() -> None:
    """write で dtype が不一致の場合に例外が発生することを確認する"""
    with pa.open_output(format=pa.SampleFormat.FLOAT32) as stream:
        data = np.zeros((1024, 1), dtype=np.int16)
        with pytest.raises(RuntimeError, match="dtype mismatch"):
            stream.write(data)


@requires_output_device
def test_write_channel_mismatch() -> None:
    """write でチャンネル数が不一致の場合に例外が発生することを確認する"""
    with pa.open_output(format=pa.SampleFormat.FLOAT32, channels=1) as stream:
        data = np.zeros((1024, 2), dtype=np.float32)
        with pytest.raises(RuntimeError, match="channel count mismatch"):
            stream.write(data)


# ========== INT24/INT8 未対応テスト ==========


def test_sample_format_no_int24() -> None:
    """SampleFormat に INT24 が存在しないことを確認する"""
    assert not hasattr(pa.SampleFormat, "INT24")


def test_sample_format_no_int8() -> None:
    """SampleFormat に INT8 が存在しないことを確認する"""
    assert not hasattr(pa.SampleFormat, "INT8")

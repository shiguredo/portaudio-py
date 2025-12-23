"""Free-Threading 対応のテスト

Python 3.13 以降の Free-Threading モードで PortAudio が正しく動作することを確認する。
複数スレッドから同時に API を呼び出しても安全であることを検証する。
"""

import concurrent.futures
import sys
import threading

import pytest

import portaudio as pa


def has_audio_device() -> bool:
    """オーディオデバイスがあるかどうかを確認する"""
    return len(pa.list_devices()) > 0


def is_free_threading_enabled() -> bool:
    """Free-Threading が有効かどうかを確認する"""
    if hasattr(sys, "_is_gil_enabled"):
        return not sys._is_gil_enabled()
    return False


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


# ========== 基本的なスレッドセーフテスト ==========


def test_concurrent_get_version() -> None:
    """複数スレッドから get_version を同時に呼び出せることを確認する"""
    results: list[int] = []
    errors: list[Exception] = []
    lock = threading.Lock()

    def get_version_task() -> None:
        try:
            version = pa.get_version()
            with lock:
                results.append(version)
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=get_version_task) for _ in range(10)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"
    assert len(results) == 10
    assert all(v == results[0] for v in results)


def test_concurrent_get_version_text() -> None:
    """複数スレッドから get_version_text を同時に呼び出せることを確認する"""
    results: list[str] = []
    errors: list[Exception] = []
    lock = threading.Lock()

    def get_version_text_task() -> None:
        try:
            text = pa.get_version_text()
            with lock:
                results.append(text)
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=get_version_text_task) for _ in range(10)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"
    assert len(results) == 10
    assert all(t == results[0] for t in results)


def test_concurrent_get_device_count() -> None:
    """複数スレッドから get_device_count を同時に呼び出せることを確認する"""
    results: list[int] = []
    errors: list[Exception] = []
    lock = threading.Lock()

    def get_device_count_task() -> None:
        try:
            count = pa.get_device_count()
            with lock:
                results.append(count)
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=get_device_count_task) for _ in range(10)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"
    assert len(results) == 10
    assert all(c == results[0] for c in results)


def test_concurrent_list_devices() -> None:
    """複数スレッドから list_devices を同時に呼び出せることを確認する"""
    results: list[list] = []
    errors: list[Exception] = []
    lock = threading.Lock()

    def list_devices_task() -> None:
        try:
            devices = pa.list_devices()
            with lock:
                results.append(devices)
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=list_devices_task) for _ in range(10)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"
    assert len(results) == 10
    assert all(len(r) == len(results[0]) for r in results)


def test_concurrent_get_host_api_count() -> None:
    """複数スレッドから get_host_api_count を同時に呼び出せることを確認する"""
    results: list[int] = []
    errors: list[Exception] = []
    lock = threading.Lock()

    def get_host_api_count_task() -> None:
        try:
            count = pa.get_host_api_count()
            with lock:
                results.append(count)
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=get_host_api_count_task) for _ in range(10)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"
    assert len(results) == 10
    assert all(c == results[0] for c in results)


# ========== ThreadPoolExecutor を使用したテスト ==========


def test_thread_pool_get_version() -> None:
    """ThreadPoolExecutor で get_version を並列実行できることを確認する"""

    def task() -> int:
        return pa.get_version()

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(task) for _ in range(20)]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]

    assert len(results) == 20
    assert all(v == results[0] for v in results)


def test_thread_pool_list_devices() -> None:
    """ThreadPoolExecutor で list_devices を並列実行できることを確認する"""

    def task() -> list:
        return pa.list_devices()

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(task) for _ in range(20)]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]

    assert len(results) == 20
    assert all(len(r) == len(results[0]) for r in results)


def test_thread_pool_get_device_info() -> None:
    """ThreadPoolExecutor で get_device_info を並列実行できることを確認する"""
    device_count = pa.get_device_count()
    if device_count == 0:
        pytest.skip("デバイスがありません")

    def task(index: int) -> pa.DeviceInfo | None:
        return pa.get_device_info(index)

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(task, i % device_count) for i in range(20)]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]

    assert len(results) == 20
    assert all(r is not None for r in results)


# ========== ストリーム関連のスレッドセーフテスト ==========


@requires_input_device
def test_concurrent_open_close_input_stream() -> None:
    """複数スレッドから入力ストリームを開閉できることを確認する"""
    errors: list[Exception] = []
    lock = threading.Lock()

    def open_close_task() -> None:
        try:
            stream = pa.open_input()
            stream.close()
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=open_close_task) for _ in range(5)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"


@requires_output_device
def test_concurrent_open_close_output_stream() -> None:
    """複数スレッドから出力ストリームを開閉できることを確認する"""
    errors: list[Exception] = []
    lock = threading.Lock()

    def open_close_task() -> None:
        try:
            stream = pa.open_output()
            stream.close()
        except Exception as e:
            with lock:
                errors.append(e)

    threads = [threading.Thread(target=open_close_task) for _ in range(5)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"


# ========== 混合操作のスレッドセーフテスト ==========


def test_concurrent_mixed_operations() -> None:
    """複数スレッドから異なる API を同時に呼び出せることを確認する"""
    errors: list[Exception] = []
    lock = threading.Lock()

    def task_get_version() -> None:
        try:
            pa.get_version()
        except Exception as e:
            with lock:
                errors.append(e)

    def task_get_device_count() -> None:
        try:
            pa.get_device_count()
        except Exception as e:
            with lock:
                errors.append(e)

    def task_list_devices() -> None:
        try:
            pa.list_devices()
        except Exception as e:
            with lock:
                errors.append(e)

    def task_get_host_api_count() -> None:
        try:
            pa.get_host_api_count()
        except Exception as e:
            with lock:
                errors.append(e)

    threads = []
    for _ in range(5):
        threads.append(threading.Thread(target=task_get_version))
        threads.append(threading.Thread(target=task_get_device_count))
        threads.append(threading.Thread(target=task_list_devices))
        threads.append(threading.Thread(target=task_get_host_api_count))

    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(errors) == 0, f"エラーが発生しました: {errors}"


# ========== Free-Threading 固有のテスト ==========


@pytest.mark.skipif(
    not is_free_threading_enabled(),
    reason="Free-Threading が有効ではありません",
)
def test_free_threading_enabled() -> None:
    """Free-Threading モードで動作していることを確認する"""
    assert is_free_threading_enabled()
    version = pa.get_version()
    assert version > 0


@pytest.mark.skipif(
    not is_free_threading_enabled(),
    reason="Free-Threading が有効ではありません",
)
def test_free_threading_concurrent_heavy_load() -> None:
    """Free-Threading モードで高負荷な並列アクセスが安全であることを確認する"""

    def task() -> None:
        for _ in range(100):
            pa.get_version()
            pa.get_device_count()
            pa.list_devices()

    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
        futures = [executor.submit(task) for _ in range(10)]
        for future in concurrent.futures.as_completed(futures):
            future.result()

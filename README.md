# portaudio-py

[![PyPI](https://img.shields.io/pypi/v/portaudio-py)](https://pypi.org/project/portaudio-py/)
[![SPEC 0 — Minimum Supported Dependencies](https://img.shields.io/badge/SPEC-0-green?labelColor=%23004811&color=%235CA038)](https://scientific-python.org/specs/spec-0000/)
[![image](https://img.shields.io/pypi/pyversions/portaudio-py.svg)](https://pypi.python.org/pypi/portaudio-py)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Actions status](https://github.com/shiguredo/portaudio-py/workflows/wheel/badge.svg)](https://github.com/shiguredo/portaudio-py/actions)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read <https://github.com/shiguredo/oss/blob/master/README.en.md> before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に <https://github.com/shiguredo/oss> をお読みください。

## portaudio-py について

[PortAudio](https://www.portaudio.com/) の Python バインディングです。

- 高レベル API と低レベル API の両方を提供
- [numpy.ndarray](https://numpy.org/doc/stable/reference/generated/numpy.ndarray.html) での音声データ取得/書き込み
- Python [Free Threading](https://docs.python.org/3/howto/free-threading-python.html) 対応

## 対応サンプルフォーマット

| フォーマット | 説明 |
|--------------|------|
| FLOAT32 | 32 ビット浮動小数点 |
| INT32 | 32 ビット符号付き整数 |
| INT24 | 24 ビット符号付き整数 |
| INT16 | 16 ビット符号付き整数 |
| INT8 | 8 ビット符号付き整数 |
| UINT8 | 8 ビット符号なし整数 |

## 対応プラットフォーム

- macOS 26 arm64
- macOS 15 arm64
- Ubuntu 24.04 x86_64
- Ubuntu 24.04 arm64
- Ubuntu 22.04 x86_64
- Ubuntu 22.04 arm64
- Windows Server 2025 x86_64
- Windows 11 x86_64

| OS | API |
| --- | --- |
| macOS | Core Audio |
| Linux | ALSA |
| Windows | WASAPI |

## 対応 Python

- 3.14
- 3.14t
- 3.13
- 3.13t
- 3.12

## インストール

```bash
uv add portaudio-py
```

## 使い方

### デバイス一覧の取得

```python
import portaudio as pa

devices = pa.list_devices()
for device in devices:
    print(f"[{device.index}] {device.name}")
    print(f"  入力: {device.max_input_channels}ch, 出力: {device.max_output_channels}ch")
```

### 入力デバイス一覧の取得

```python
import portaudio as pa

devices = pa.list_input_devices()
for device in devices:
    print(f"[{device.index}] {device.name} ({device.max_input_channels}ch)")
```

### 出力デバイス一覧の取得

```python
import portaudio as pa

devices = pa.list_output_devices()
for device in devices:
    print(f"[{device.index}] {device.name} ({device.max_output_channels}ch)")
```

### 音声入力

```python
import portaudio as pa

with pa.open_input() as stream:
    data = stream.read(1024)
    print(f"Shape: {data.shape}, Dtype: {data.dtype}")
```

### 音声出力

```python
import numpy as np
import portaudio as pa

with pa.open_output() as stream:
    # 440Hz のサイン波を生成
    sample_rate = 44100
    duration = 1.0
    t = np.linspace(0, duration, int(sample_rate * duration), dtype=np.float32)
    data = np.sin(2 * np.pi * 440 * t).reshape(-1, 1)
    stream.write(data)
```

### フォーマット指定

```python
import portaudio as pa

with pa.open_input(format=pa.SampleFormat.INT16) as stream:
    data = stream.read(1024)
    print(f"Dtype: {data.dtype}")  # int16
```

### デバイス指定

```python
import portaudio as pa

devices = pa.list_input_devices()
if devices:
    with pa.open_input(device=devices[0]) as stream:
        data = stream.read(1024)
```

## API リファレンス

### モジュール関数

| 関数 | 説明 |
|------|------|
| `list_devices()` | 全てのオーディオデバイス一覧を取得 |
| `list_input_devices()` | 入力デバイス一覧を取得 |
| `list_output_devices()` | 出力デバイス一覧を取得 |
| `open_input(device, sample_rate, channels, format, frames_per_buffer)` | 入力ストリームを開く |
| `open_output(device, sample_rate, channels, format, frames_per_buffer)` | 出力ストリームを開く |

#### open_input / open_output の引数

| 引数 | 型 | デフォルト | 説明 |
|------|-----|-----------|------|
| `device` | DeviceInfo \| None | None | デバイス (None でデフォルトデバイス) |
| `sample_rate` | float | 44100.0 | サンプルレート |
| `channels` | int | 1 | チャンネル数 |
| `format` | SampleFormat | FLOAT32 | サンプルフォーマット |
| `frames_per_buffer` | int | 1024 | バッファサイズ |

### Stream

オーディオストリームを操作するクラス。コンテキストマネージャとして使用可能。

```python
with pa.open_input() as stream:
    data = stream.read(1024)
```

| メソッド | 説明 |
|----------|------|
| `read(frames)` | 指定フレーム数を読み込み (入力ストリームのみ) |
| `write(buffer)` | バッファを書き込み (出力ストリームのみ) |
| `start()` | ストリームを開始 |
| `stop()` | ストリームを停止 |
| `abort()` | ストリームを中断 |
| `close()` | ストリームを閉じる |
| `is_active()` | アクティブかどうか |
| `is_stopped()` | 停止中かどうか |
| `get_time()` | ストリーム時間を取得 |
| `get_cpu_load()` | CPU 負荷を取得 |
| `get_read_available()` | 読み込み可能なフレーム数 |
| `get_write_available()` | 書き込み可能なフレーム数 |
| `get_info()` | ストリーム情報を取得 |

| プロパティ | 説明 |
|------------|------|
| `sample_rate` | サンプルレート |
| `input_channels` | 入力チャンネル数 |
| `output_channels` | 出力チャンネル数 |
| `format` | サンプルフォーマット |

#### read の戻り値

- numpy.ndarray (shape: `(frames, channels)`)
- dtype はフォーマットに応じて自動設定 (float32, int32, int16, int8, uint8)

#### write の引数

- numpy.ndarray (shape: `(frames, channels)`)
- dtype はストリームのフォーマットと一致している必要あり

### DeviceInfo

デバイス情報を表すクラス。

| プロパティ | 説明 |
|------------|------|
| `index` | デバイスインデックス |
| `name` | デバイス名 |
| `max_input_channels` | 最大入力チャンネル数 |
| `max_output_channels` | 最大出力チャンネル数 |
| `default_sample_rate` | デフォルトサンプルレート |
| `default_low_input_latency` | デフォルト低入力レイテンシ |
| `default_high_input_latency` | デフォルト高入力レイテンシ |
| `default_low_output_latency` | デフォルト低出力レイテンシ |
| `default_high_output_latency` | デフォルト高出力レイテンシ |

### SampleFormat

サンプルフォーマットを表す列挙型。

| 値 | 説明 |
|-----|------|
| `FLOAT32` | 32 ビット浮動小数点 |
| `INT32` | 32 ビット符号付き整数 |
| `INT24` | 24 ビット符号付き整数 |
| `INT16` | 16 ビット符号付き整数 |
| `INT8` | 8 ビット符号付き整数 |
| `UINT8` | 8 ビット符号なし整数 |

## サンプル

`examples/` ディレクトリにサンプルコードがあります。

```bash
# マイクから音声をキャプチャして WAV ファイルに保存
uv run python examples/capture.py

# WAV ファイルを再生
uv run python examples/playback.py

# マイク音声を raw_player でリアルタイム再生
uv run python examples/monitor.py
```

## ビルド

```bash
make develop
```

## Discord

- **サポートはしません**
- アドバイスします
- フィードバック歓迎します

最新の状況などは Discord で共有しています。質問や相談も Discord でのみ受け付けています。

<https://discord.gg/shiguredo>

## バグ報告

Discord へお願いします。

## PortAudio ライセンス

MIT License

```text
PortAudio Portable Real-Time Audio Library
PortAudio API Header File
Latest version available at: http://www.portaudio.com

Copyright (c) 1999-2006 Ross Bencina and Phil Burk

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## ライセンス

Apache License 2.0

```text
Copyright 2025-2025, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

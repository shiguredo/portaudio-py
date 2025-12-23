"""マイクから音声をキャプチャするサンプル"""

import numpy as np

import portaudio as pa


def main() -> None:
    # デバイス情報を表示
    print("=== 入力デバイス一覧 ===")
    for idx, dev in pa.get_input_devices():
        print(
            f"  [{idx}] {dev.name} (Ch: {dev.max_input_channels}, Rate: {dev.default_sample_rate})"
        )

    # デフォルト入力デバイスを取得
    input_device = pa.get_default_input_device()
    if input_device == pa.NO_DEVICE:
        print("入力デバイスが見つかりません")
        return

    device_info = pa.get_device_info(input_device)
    if device_info is None:
        print("デバイス情報の取得に失敗しました")
        return

    print(f"\n使用デバイス: [{input_device}] {device_info.name}")

    # パラメータ設定
    sample_rate = 48000
    channels = 1
    frames_per_buffer = 1024
    duration_sec = 3

    # 入力パラメータを作成
    input_params = pa.StreamParameters(
        device=input_device,
        channel_count=channels,
        sample_format=pa.FLOAT32,
        suggested_latency=device_info.default_low_input_latency,
    )

    print(f"\nサンプルレート: {sample_rate} Hz")
    print(f"チャンネル数: {channels}")
    print(f"フレーム/バッファ: {frames_per_buffer}")
    print(f"録音時間: {duration_sec} 秒")

    # 録音データを格納するリスト
    recorded_frames: list[np.ndarray] = []

    # ストリームを開いてキャプチャ
    print(f"\n録音開始... ({duration_sec}秒間)")

    with pa.Stream(
        input_parameters=input_params,
        sample_rate=sample_rate,
        frames_per_buffer=frames_per_buffer,
    ) as stream:
        total_frames = int(sample_rate * duration_sec)
        captured_frames = 0

        while captured_frames < total_frames:
            # float32 形式で読み込み (shape: [frames, channels])
            data = stream.read_float32(frames_per_buffer)
            recorded_frames.append(data)
            captured_frames += data.shape[0]

            # 進捗表示
            progress = min(100, int(captured_frames / total_frames * 100))
            print(f"\r録音中: {progress}%", end="", flush=True)

    print("\n録音完了!")

    # 全フレームを結合
    audio_data = np.concatenate(recorded_frames, axis=0)
    print(f"\n録音データ形状: {audio_data.shape}")
    print(f"データ型: {audio_data.dtype}")
    print(f"最小値: {audio_data.min():.4f}")
    print(f"最大値: {audio_data.max():.4f}")
    print(f"RMS: {np.sqrt(np.mean(audio_data**2)):.4f}")

    # WAV ファイルとして保存 (オプション)
    try:
        import wave

        output_file = "captured.wav"
        with wave.open(output_file, "wb") as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(2)  # 16-bit
            wf.setframerate(sample_rate)
            # float32 を int16 に変換
            int16_data = (audio_data * 32767).astype(np.int16)
            wf.writeframes(int16_data.tobytes())
        print(f"\n保存完了: {output_file}")
    except Exception as e:
        print(f"\nWAV 保存エラー: {e}")


if __name__ == "__main__":
    main()

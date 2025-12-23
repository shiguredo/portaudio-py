"""WAV ファイルを再生するサンプル"""

import sys
import wave

import numpy as np

import portaudio as pa


def main() -> None:
    # 引数でファイル名を指定、なければデフォルト
    if len(sys.argv) > 1:
        wav_file = sys.argv[1]
    else:
        wav_file = "captured.wav"

    # WAV ファイルを読み込み
    try:
        with wave.open(wav_file, "rb") as wf:
            channels = wf.getnchannels()
            sample_width = wf.getsampwidth()
            sample_rate = wf.getframerate()
            n_frames = wf.getnframes()
            raw_data = wf.readframes(n_frames)
    except FileNotFoundError:
        print(f"ファイルが見つかりません: {wav_file}")
        print("先に capture.py を実行してください")
        return

    print(f"ファイル: {wav_file}")
    print(f"チャンネル数: {channels}")
    print(f"サンプル幅: {sample_width} bytes")
    print(f"サンプルレート: {sample_rate} Hz")
    print(f"フレーム数: {n_frames}")
    print(f"再生時間: {n_frames / sample_rate:.2f} 秒")

    # サンプル幅に応じて numpy 配列に変換
    if sample_width == 2:
        audio_data = np.frombuffer(raw_data, dtype=np.int16)
        # float32 に変換して正規化
        audio_float = audio_data.astype(np.float32) / 32768.0
    elif sample_width == 4:
        audio_data = np.frombuffer(raw_data, dtype=np.int32)
        audio_float = audio_data.astype(np.float32) / 2147483648.0
    else:
        print(f"サポートされていないサンプル幅: {sample_width}")
        return

    # [frames, channels] の形状にリシェイプ
    audio_float = audio_float.reshape(-1, channels)

    # デフォルト出力デバイスを取得
    output_device = pa.get_default_output_device()
    if output_device == pa.NO_DEVICE:
        print("出力デバイスが見つかりません")
        return

    device_info = pa.get_device_info(output_device)
    if device_info is None:
        print("デバイス情報の取得に失敗しました")
        return

    print(f"\n使用デバイス: [{output_device}] {device_info.name}")

    # 出力パラメータを作成
    output_params = pa.StreamParameters(
        device=output_device,
        channel_count=channels,
        sample_format=pa.FLOAT32,
        suggested_latency=device_info.default_low_output_latency,
    )

    frames_per_buffer = 1024

    # ストリームを開いて再生
    print("\n再生開始...")

    with pa.Stream(
        output_parameters=output_params,
        sample_rate=sample_rate,
        frames_per_buffer=frames_per_buffer,
    ) as stream:
        total_frames = audio_float.shape[0]
        played_frames = 0

        while played_frames < total_frames:
            # バッファサイズ分のデータを取得
            end_frame = min(played_frames + frames_per_buffer, total_frames)
            chunk = audio_float[played_frames:end_frame]

            # 足りない分はゼロパディング
            if chunk.shape[0] < frames_per_buffer:
                padding = np.zeros((frames_per_buffer - chunk.shape[0], channels), dtype=np.float32)
                chunk = np.concatenate([chunk, padding], axis=0)

            # float32 形式で書き込み
            stream.write_float32(chunk)
            played_frames = end_frame

            # 進捗表示
            progress = int(played_frames / total_frames * 100)
            print(f"\r再生中: {progress}%", end="", flush=True)

    print("\n再生完了!")


if __name__ == "__main__":
    main()

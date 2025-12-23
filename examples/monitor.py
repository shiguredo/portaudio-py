"""マイクから取得した音声を raw_player でリアルタイム再生するサンプル"""

import signal

from raw_player import AudioPlayer

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
    frames_per_buffer = 512

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

    # AudioPlayer を作成
    player = AudioPlayer()

    # 終了フラグ
    running = True

    def signal_handler(signum: int, frame: object) -> None:
        nonlocal running
        running = False
        print("\n終了します...")

    signal.signal(signal.SIGINT, signal_handler)

    print("\nモニタリング開始... (Ctrl+C で終了)")

    # プレゼンテーションタイムスタンプ (マイクロ秒)
    pts_us = 0

    with pa.Stream(
        input_parameters=input_params,
        sample_rate=sample_rate,
        frames_per_buffer=frames_per_buffer,
    ) as stream:
        # 再生開始
        player.play()

        while running:
            # float32 形式で読み込み (shape: [frames, channels])
            data = stream.read_float32(frames_per_buffer)

            # raw_player に送信
            player.enqueue_audio(data, pts_us, sample_rate)

            # pts を更新 (マイクロ秒単位)
            pts_us += int(data.shape[0] * 1_000_000 / sample_rate)

            # 統計情報を表示
            stats = player.stats()
            buffer_ms = stats.get("audio_buffer_ms", 0)
            print(f"\rバッファ: {buffer_ms:.1f} ms", end="", flush=True)

    # 停止
    player.stop()
    print("\n完了!")


if __name__ == "__main__":
    main()

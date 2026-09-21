import argparse
import csv
import serial
import sys
import time
from datetime import datetime

DEFAULT_BAUD = 921600
DEFAULT_PORT = "COM5"


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Raw MPU6050 serial data logger."
    )

    parser.add_argument(
        "-p",
        "--port",
        default=DEFAULT_PORT,
        help=f"Serial port (default: {DEFAULT_PORT})",
    )

    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"Baud rate (default: {DEFAULT_BAUD})",
    )

    parser.add_argument(
        "-l",
        "--label",
        required=True,
        choices=["healthy", "imbalanced", "obstruction"],
        help="Physical condition during this recording session.",
    )

    parser.add_argument(
        "-s",
        "--session",
        default="default",
        help="Session identifier, e.g. table, floor, reserved.",
    )

    parser.add_argument(
        "-d",
        "--duration",
        type=int,
        default=0,
        help="Recording duration in seconds. 0 = run until Ctrl+C.",
    )

    return parser.parse_args()


def main():
    args = parse_arguments()

    # Each recording gets its own file to avoid mixing sessions.
    session_time = datetime.now().strftime("%Y%m%d_%H%M%S")

    filename = (
        f"raw_{args.label}_{args.session}_{session_time}.csv"
    )

    headers = [
        "timestamp_ms",
        "accX_raw",
        "accY_raw",
        "accZ_raw",
        "label",
    ]

    print("=" * 70)
    print("SERIAL DATA LOGGER")
    print("=" * 70)
    print(f"Port      : {args.port}")
    print(f"Baud      : {args.baud}")
    print(f"Label     : {args.label}")
    print(f"Session   : {args.session}")
    print(f"Output    : {filename}")

    if args.duration > 0:
        print(f"Duration  : {args.duration} seconds")
    else:
        print("Duration  : Until Ctrl+C")

    print("=" * 70)

    try:
        ser = serial.Serial(
            args.port,
            args.baud,
            timeout=1.0
        )

        # ESP32 may reset when Serial opens.
        time.sleep(2)

        # Ignore boot messages and old buffered bytes.
        ser.reset_input_buffer()

    except serial.SerialException as error:
        print(f"[ERROR] Cannot open {args.port}: {error}")
        sys.exit(1)

    sample_count = 0
    malformed_count = 0
    start_time = time.monotonic()

    try:
        with open(
            filename,
            mode="w",
            newline="",
            encoding="utf-8"
        ) as csv_file:

            writer = csv.writer(csv_file)
            writer.writerow(headers)

            while True:
                elapsed = time.monotonic() - start_time

                # Optional automatic stop.
                if args.duration > 0 and elapsed >= args.duration:
                    break

                line = ser.readline().decode(
                    "utf-8",
                    errors="ignore"
                ).strip()

                if not line:
                    continue

                parts = line.split(",")

                # Firmware contract:
                # timestamp_ms, accX_raw, accY_raw, accZ_raw
                if len(parts) != 4:
                    malformed_count += 1
                    continue

                try:
                    timestamp_ms = float(parts[0])
                    acc_x = float(parts[1])
                    acc_y = float(parts[2])
                    acc_z = float(parts[3])

                except ValueError:
                    malformed_count += 1
                    continue

                writer.writerow([
                    timestamp_ms,
                    acc_x,
                    acc_y,
                    acc_z,
                    args.label,
                ])

                sample_count += 1

                # Flush periodically without flushing every sample.
                if sample_count % 500 == 0:
                    csv_file.flush()

                    elapsed = time.monotonic() - start_time

                    print(
                        f"Samples: {sample_count} | "
                        f"Elapsed: {elapsed:.1f}s | "
                        f"Malformed: {malformed_count}",
                        end="\r",
                    )

    except KeyboardInterrupt:
        print("\nLogging stopped by user.")

    finally:
        if ser.is_open:
            ser.close()

    elapsed = time.monotonic() - start_time

    print("\n" + "=" * 70)
    print("SESSION COMPLETE")
    print(f"Samples saved   : {sample_count}")
    print(f"Malformed lines : {malformed_count}")
    print(f"Elapsed time    : {elapsed:.1f} seconds")
    print(f"Output file     : {filename}")
    print("=" * 70)


if __name__ == "__main__":
    main()
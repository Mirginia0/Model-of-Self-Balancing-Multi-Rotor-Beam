from datetime import datetime
import serial
import threading

from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

PORT = "COM3"
BAUD = 115200
OUTFILE = "log.txt"

# True: nie pokazuje LOG na ekranie (ale zapisuje do log.txt)
HIDE_LOG_LINES_ON_SCREEN = True

HELP_TEXT = """
=== KOMENDY (wysyłane do ESP32) ===
s         -> START silników (EN=1)
x         -> STOP silników (EN=0)
t<deg>    -> target, np: t30, t-15, t0

kp<val>   -> ustaw Kp, np: kp10, kp6.5
ki<val>   -> ustaw Ki, np: ki0, ki0.2
kd<val>   -> ustaw Kd, np: kd0, kd0.8

a+ / a-   -> trim ESC1 (pin 26) +/-5us
b+ / b-   -> trim ESC2 (pin 25) +/-5us
ta<us>    -> ustaw TRIM_ESC1_US, np: ta-15, ta20
tb<us>    -> ustaw TRIM_ESC2_US, np: tb10, tb-25

p         -> pokaż target/trim/Kp/Ki/Kd (po stronie ESP32)
h         -> help (po stronie ESP32)

help      -> pokaż tę legendę (po stronie Pythona)
quit      -> wyjście z programu
===================================

LOG zapisuje do pliku: log.txt
Format LOG (nowy): LOG,ms,roll,target,kp,ki,kd,pwm1,pwm2
"""


def reader(ser: serial.Serial, logfile):
    while True:
        raw = ser.readline()
        if not raw:
            continue

        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        if line.startswith("LOG,"):
            # zapisuj zawsze
            logfile.write(line + "\n")
            logfile.flush()

            # opcjonalnie pokaż na ekranie
            if not HIDE_LOG_LINES_ON_SCREEN:
                print(line)
            else:
                # lekka walidacja: oczekujemy 9 pól: LOG + 8 wartości
                parts = line.split(",")
                if len(parts) != 9:
                    # nie spamuj logami, ale pokaż problem
                    print(f"[WARN] Nieoczekiwany format LOG ({len(parts)} pól): {line}")

        else:
            print(line)


def main():
    print(f"Łączę się z: {PORT} @ {BAUD}")
    print(HELP_TEXT)

    with serial.Serial(PORT, BAUD, timeout=1) as ser, open(OUTFILE, "a", buffering=1, encoding="utf-8") as f:
        stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        f.write(f"\n--- START LOG {stamp} ---\n")

        t_read = threading.Thread(target=reader, args=(ser, f), daemon=True)
        t_read.start()

        session = PromptSession()

        with patch_stdout():
            while True:
                cmd = session.prompt("> ").strip()
                if not cmd:
                    continue

                low = cmd.lower()
                if low in ("quit", "exit"):
                    break
                if low == "help":
                    print(HELP_TEXT)
                    continue

                # wysyłamy dokładnie to, co wpisano (ESP32 parsuje komendy)
                ser.write((cmd + "\n").encode("utf-8"))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nZatrzymano (Ctrl+C).")

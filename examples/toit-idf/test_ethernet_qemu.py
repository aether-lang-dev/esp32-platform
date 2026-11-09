"""Boot a merged Ethernet QEMU image and require DHCP and provider teardown."""
import subprocess
import sys


def main(qemu, flash):
    command = [
        qemu, "-nographic", "-machine", "esp32",
        "-drive", f"file={flash},if=mtd,format=raw", "-nic", "user,model=open_eth",
    ]
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
        got_address = False
        try:
            for line in process.stdout:
                print(line, end="", flush=True)
                if line.startswith("Ethernet address "):
                    got_address = True
                if line.startswith("Ethernet provider closed"):
                    if not got_address:
                        raise SystemExit("Ethernet client did not obtain an address")
                    return
            raise SystemExit("QEMU exited before provider teardown")
        finally:
            process.terminate()


if __name__ == "__main__":
    main(*sys.argv[1:])

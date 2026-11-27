import asyncio
import sys
import bleak

LED_CHR_UUID = "12345678-1234-5678-1234-56789abcdef1"

def on_led_notify(sender, data: bytearray):
    print(f'[NOTIFY] Got notification, sender: {sender}')
    print(f'[NOTIFY] Got notification: {list(data)} (raw: {data.hex()})')

async def _main():
    while True:
        device = await bleak.BleakScanner.find_device_by_filter(
            lambda d, adv: (adv.local_name or "").startswith("CAN-"),
            timeout=10.0,
        )
        if device is not None:
            print(f'start to connect: {device.name}, {device.address}')

            async with bleak.BleakClient(device) as client:
                print(f'Connect success! connected={client.is_connected}, MTU={client.mtu_size}')
                
                # Read current LED status
                val = await client.read_gatt_char(LED_CHR_UUID)
                print(val)
                print(f'[READ] Current LED status: {val[0]}')

                # Enable notify
                await client.start_notify(LED_CHR_UUID, on_led_notify)
                print(f'[NOTIFY] LED notify booked')

                # Read keyboard input asynchronously, and send command to write
                loop = asyncio.get_running_loop()
                while True:
                    cmd = await loop.run_in_executor(None, input, '> ')
                    cmd = cmd.strip().lower()
                    if cmd == 'q':
                        break
                    elif cmd == '1':
                        await client.write_gatt_char(LED_CHR_UUID, b'\x01', response=True)
                    elif cmd == '0':
                        await client.write_gatt_char(LED_CHR_UUID, b'\x00', response=True)
                    elif cmd == 'r':
                        val = await client.read_gatt_char(LED_CHR_UUID)
                        print(f'[READ] Current LED status: {val[0]}')


            break

    exit(0)


def main() -> None:
    asyncio.run(_main())

if __name__ == '__main__':
    sys.exit(main())

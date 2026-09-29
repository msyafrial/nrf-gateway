import serial, threading, time, requests, sys

def serial_reader(stop_ev):
    try:
        ser = serial.Serial('COM10', 115200, timeout=0.1)
        while not stop_ev.is_set():
            line = ser.readline()
            if line:
                try:
                    print("[SERIAL]", line.decode('utf-8', errors='replace').strip())
                except:
                    pass
        ser.close()
    except Exception as ex:
        print("[SERIAL ERR]", ex)

stop_ev = threading.Event()
t = threading.Thread(target=serial_reader, args=(stop_ev,))
t.start()

time.sleep(1)

session = requests.Session()
res = session.post("http://192.168.8.161/api/v1/auth/login", data={"username": "admin", "password": "admin"})
print("Login status:", res.status_code, res.text)

fw_path = "c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/.pio/build/esp32doit-devkit-v1/firmware.bin"
with open(fw_path, "rb") as f:
    data = f.read()

print(f"Firmware size: {len(data)} bytes")

upload_url = f"http://192.168.8.161/api/v1/ota/upload?type=firmware&size={len(data)}"
headers = {"Content-Type": "application/octet-stream"}
res_up = session.post(upload_url, data=data, headers=headers)
print("Upload status:", res_up.status_code)
print("Upload text:", repr(res_up.text))

time.sleep(2)
stop_ev.set()
t.join()

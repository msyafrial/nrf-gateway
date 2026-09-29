import serial, time, sys

try:
    ser = serial.Serial('COM10', 115200, timeout=1)
    time.sleep(0.5)
    data = ser.read_all()
    sys.stdout.buffer.write(data)
    ser.close()
except Exception as e:
    print("Error opening COM10:", e)

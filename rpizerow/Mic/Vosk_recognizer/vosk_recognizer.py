#!/usr/bin/python3
import serial
from vosk import Model, KaldiRecognizer
import json

ser = serial.Serial('/dev/serial0', 115200, timeout=1)

model = Model("Vosk_model/vosk-model-small-es-0.42")
rec = KaldiRecognizer(model, 16000)

print("Esperando audio PCM por UART...")

while True:
    data = ser.read(4000)  # leer 4000 bytes (2 bytes por muestra, 2000 muestras)
    if len(data) == 0:
        continue

    if rec.AcceptWaveform(data):
        result = json.loads(rec.Result())
        texto = result.get("text", "")
        if texto:
            print("🗣️", texto)
    else:
        # Resultados parciales pueden ir aquí si querés
        pass


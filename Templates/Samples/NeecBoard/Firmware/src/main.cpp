#include <Arduino.h>
#include <ChainableLED.h>
#include <Servo.h>

namespace {
	ChainableLED led(A0, A1, 1);
	Servo servo;
	bool paired = false, pressed = false, raw = false;
	unsigned long changedAt = 0, measuredAt = 0;
	char line[48];
	size_t length = 0;
	bool overflow = false;

	void command() {
		int r, g, b;
		char extra;
		if (strcmp(line, "HELLO 1") == 0) {
			paired = true;
			Serial.print(F("NEEC 1 "));
			Serial.println(pressed ? 1 : 0);
		} else if (paired && sscanf(line, "LED %d %d %d %c", &r, &g, &b, &extra) == 3 &&
			r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
			led.setColorRGB(0, r, g, b);
			Serial.println(F("OK LED"));
		} else if (paired && sscanf(line, "SERVO %d %c", &r, &extra) == 1 && r >= 10 && r <= 170) {
			if (!servo.attached()) servo.attach(6);
			servo.write(r);
			Serial.println(F("OK SERVO"));
		}
	}

	void receive() {
		while (Serial.available()) {
			char c = Serial.read();
			if (c == '\n') {
				line[length] = '\0';
				if (!overflow) command();
				length = 0;
				overflow = false;
			} else if (c != '\r') {
				if (length < sizeof(line) - 1) line[length++] = c;
				else overflow = true;
			}
		}
	}
}

void setup() {
	pinMode(2, INPUT);
	raw = pressed = digitalRead(2) == HIGH;
	led.init();
	Serial.begin(115200);
}

void loop() {
	receive();
	const unsigned long now = millis();
	bool value = digitalRead(2) == HIGH;
	if (value != raw) { raw = value; changedAt = now; }
	if (pressed != raw && now - changedAt >= 30) {
		pressed = raw;
		if (paired) {
			Serial.print(F("BUTTON "));
			Serial.println(pressed ? 1 : 0);
		}
	}
	if (!paired || now - measuredAt < 60) return;
	measuredAt = now;
	pinMode(4, OUTPUT);
	digitalWrite(4, LOW);
	delayMicroseconds(2);
	digitalWrite(4, HIGH);
	delayMicroseconds(5);
	digitalWrite(4, LOW);
	pinMode(4, INPUT);
	const unsigned long pulse = pulseIn(4, HIGH, 25000);
	Serial.print(F("DIST "));
	Serial.println(pulse ? long(pulse * 17 / 100) : -1L);
}


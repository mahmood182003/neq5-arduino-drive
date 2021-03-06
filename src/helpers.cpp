#include "Arduino.h"
#include "MPU6050_DMP6.h"

size_t println(const String &s) {
	Serial.println(s);
	return Serial1.println(s);
}
size_t println(double num, int digits) {
	Serial.println(num, digits);
	return Serial1.println(num, digits);
}
size_t println(float num, int digits) {
	Serial.println(num, digits);
	return Serial1.println(num, digits);
}
/*
 size_t println(long num) {
 Serial.println(num);
 return Serial1.println(num);
 }
 */

size_t println(float num) {
	Serial.println(num);
	return Serial1.println(num);
}

/*
 size_t print(long num) {
 Serial.print(num);
 return Serial1.print(num);
 }
 */
size_t print(float num) {
	Serial.print(num);
	return Serial1.print(num);
}
size_t print(double n, int digits) {
	Serial.print(n, digits);
	return Serial1.print(n, digits);
}
size_t print(const String &s) {
	Serial.print(s);
	return Serial1.print(s);
}
size_t println(const char c[]) {
	Serial.println(c);
	return Serial1.println(c);
}
size_t print(const char c[]) {
	Serial.print(c);
	return Serial1.print(c);
}

void printGravity(triple_t gravity) {
	print("Gravity ");
	print(gravity.x, 6);
	print(" ");
	print(gravity.y, 6);
	print(" ");
	print(gravity.z, 6);
	print("  ");
	print("YPR ");
	print(gravity.y, 6);
	print(" ");
	print(gravity.p, 6);
	print(" ");
	println(gravity.r, 6);
}
void printGravity(triple_t gravity, String msg) {
	if (msg.length() > 0) {
		print(msg);
	}
	printGravity(gravity);
}

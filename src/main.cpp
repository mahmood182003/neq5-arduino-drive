#include "main.h"
#include "MPU6050_DMP6.h"
#include "helpers.h"

char junk;
String inputString = "";
static FILE uartout = {0};
// Function that printf and related will use to print
int serial_putchar(char c, FILE* f) {
	if (c == '\n')
		serial_putchar('\r', f);
	Serial1.write(c);
	return Serial.write(c) == 1 ? 0 : 1;
}
// Function that printf and related will use to print
static int uart_putchar(char c, FILE *stream) {
	Serial.write(c);
	Serial1.write(c);
	return 0;
}

triple_t current_g;

int dir[2], stepModes[6] = { 1, 2, 4, 8, 16, 32 }, sm = 0, decelerating[2];
long sps[2];
const int ENABLE1 = 23, M10 = 25, M11 = 27, M12 = 29, RESET1 = 31, SLEEP1 = 33, STEP1 = 35, DIR1 =
		37;
const int ENABLE2 = 39, M20 = 41, M21 = 43, M22 = 45, RESET2 = 47, SLEEP2 = 49, STEP2 = 51, DIR2 =
		53;

const long DISTANCE = 10000000, ACCEL = 500;
const int CW = 1, CCW = -1;
const uint8_t ms_table[] = { 0b000, 0b001, 0b010, 0b011, 0b100, 0b101 };

AccelStepper stepper1(AccelStepper::DRIVER, STEP1, DIR1);
AccelStepper stepper2(AccelStepper::DRIVER, STEP2, DIR2);
AccelStepper* steppers[2];
unsigned i = 0;
long loopDuration, maxSPS; // maximum steps per second, i.e max loops per second

mount_state_t current_mstate;

unsigned setMicrostep(unsigned microsteps, unsigned i) {
	unsigned int b = 0;
	while (b < sizeof(ms_table)) {
		if (microsteps & (1 << b)) {
			uint8_t mask = ms_table[b];
			if (i == 0) {
				digitalWrite(M12, mask & 4);
				digitalWrite(M11, mask & 2);
				digitalWrite(M10, mask & 1);
			} else if (i == 1) {
				digitalWrite(M22, mask & 4);
				digitalWrite(M21, mask & 2);
				digitalWrite(M20, mask & 1);
			}
			break;
		}
		b++;
	}
	return microsteps;
}

#define ROUNDS 10
long estimateLoopTime() { // find out how long a loop() takes
	stepper1.disableOutputs();
	stepper2.disableOutputs();
	int round = ROUNDS;
	long avg = 0, time;
	while (round--) {
		time = micros();
		loop();
		avg += (micros() - time) / ROUNDS;
	}
	return avg;
	stepper1.enableOutputs();
	stepper2.enableOutputs();

}

long getSpeed(int i) {
	return sps[i];
}
// set steps per second
void setSPS(int i, long _sps) {
	if (_sps < 0)
		_sps = 1;
	if (_sps > maxSPS)
		_sps = maxSPS;
	sps[i] = _sps;
	steppers[i]->setAcceleration(3 * _sps);
	steppers[i]->setMaxSpeed(_sps);
}

long getDistance_RA(const triple_t& g1, const triple_t& g2,
	const mount_state_t &mstate) {
long result;
if (g2.RA_isLeft == mstate.RA_isLeft) {
	result = abs(acos(g2.z)-acos(g1.z)) / mstate.RA_speed;
} else {
	println(abs(acos(g2.z) + acos(g1.z)) / mstate.RA_speed, 6);
	result = abs(acos(g2.z) + acos(g1.z)) / mstate.RA_speed;
}
if (result < 0) {
	println("getDistance_RA: error! will use default");
}
return result;
}

int calcRegion(const triple_t& t1) {
//double regions[8] = { 0, -HALF_PI, HALF_PI, 0, 0, -HALF_PI, HALF_PI, 0 }; // observed when RA is tilted to left
int regionIdx = -1;
float x1 = t1.x, y1 = t1.y;
//bool RA_sameTilt = t1.RA_sameTilt(mstate.RA_isLeft);

// flip signs as if RA is left tilted (just for the sake the following rules)
if (!t1.RA_isLeft) {
	println("RA not left tilted, negating x1 and y1");
	x1 = -x1;
	y1 = -y1;
}

// rules that partition xy-plane into 4 region, presuming a left-tilted RA
if (x1 < 0 && y1 > 0) {
	regionIdx = 0;
} else if (x1 < 0 && y1 < 0) {
	regionIdx = 2;
} else if (x1 > 0 && y1 < 0) {
	regionIdx = 4;
} else if (x1 > 0 && y1 > 0) {
	regionIdx = 6;
}
return regionIdx;
}

//#define getDistance_DEC_log
long getDistance_DEC(const triple_t& g1, const triple_t& g2,
	const mount_state_t &mstate, int dir) {
float angularDist = 0;
double regions[8] = { 0, -HALF_PI, HALF_PI, 0, 0, -HALF_PI, HALF_PI, 0 }; // observed when RA is tilted to left
int begin = -1, end = -1; // the start and end regions
float x1 = g1.x, y1 = g1.y, x2 = g2.x, y2 = g2.y, phi1, phi2;
bool RA_sameTilt = g2.RA_isLeft == mstate.RA_isLeft;
// flip signs as if RA is left tilted (to be able to use the following rules)
if (!mstate.RA_isLeft) {
	println("RA not left tilted, negating x1 and y1");
	x1 = -x1;
	y1 = -y1;
}
if (!mstate.RA_isLeft && RA_sameTilt || mstate.RA_isLeft && !RA_sameTilt) { // if the target orientation is right tilted
	println("target not left tilted, negating x2 and y2");
	x2 = -x2;
	y2 = -y2;
}

begin = calcRegion(g1);
end = calcRegion(g2);
phi1 = atan(g1.x / g1.y);
phi2 = atan(g2.x / g2.y);

#ifdef getDistance_DEC_log
printf("begin=%d end=%d\n", begin, end);
println(phi1);
println(phi2);
#endif

if (begin == end) {
	angularDist += abs(phi1 - phi2);
} else {
	if (dir == CW) { // distance to the end of each region
		angularDist += abs(regions[begin + 1] - phi1);
		angularDist += abs(regions[end] - phi2);

	} else { // distance to the start of each region
		angularDist += abs(regions[begin] - phi1);
		angularDist += abs(regions[end + 1] - phi2);
	}
	// add the distance for the regions in between
	angularDist += (abs(end-begin) / 2 - 1) * HALF_PI;
}

return angularDist / mstate.DEC_speed;
}

// physical limitations on both RA ends on my NEQ5
bool RA_limit(const triple_t& gravity, int RA_dir,
	const mount_state_t &mstate) {
if ((RA_dir == CW && !mstate.RA_isLeft && gravity.z < 0.37)
		|| (RA_dir == CCW && mstate.RA_isLeft && gravity.z < 0)) {
	println("RA reached the end.");
	return true;
} else {
	return false;
}
}

// physical limitations on both DEC ends on my NEQ5
bool DEC_limit(triple_t gravity, int DEC_dir) {
float xTan = gravity.x / gravity.y;
if (DEC_dir == CW && 0 < xTan && xTan < 5.7) {
	println("DEC reached the end.");
	return true;
} else {
	return false;
}
}

//#define calcDirections_log
int RA_dir = 0, DEC_dir = 0;
void calcDirections(const triple_t& target) {
mount_state_t mstate = current_mstate;

int currentRegion = calcRegion(current_g);
int targetRegion = calcRegion(target);
bool RA_sameTilt = mstate.RA_isLeft == target.RA_isLeft;

// find out relative directions for reaching the target tilt and heading
RA_dir = (mstate.RA_isLeft ? 1 : -1)
		* ((target.z < current_g.z) && RA_sameTilt ? -1 : 1);

if (currentRegion == targetRegion) {
	if (currentRegion == 0 || currentRegion == 4) {
		DEC_dir = (abs(target.x) > abs(current_g.x)) ? CW : CCW;
	} else if (currentRegion == 2 || currentRegion == 3) {
		DEC_dir = (abs(target.x) > abs(current_g.x)) ? CCW : CW;
	}
} else {
	DEC_dir = (currentRegion < targetRegion) ? CW : CCW;
}
/*

 bool DEC_willBeLeft = (mstate.DEC_isLeft == RA_sameTilt);
 bool RA_willBeLeft = (mstate.RA_isLeft == RA_sameTilt); // if not same tilt then RA will flip its tilt
 bool DEC_sameTilt = (target.y < 0) == DEC_willBeLeft;

 bool headingBackward = (RA_willBeLeft != DEC_willBeLeft);
 bool headingBackRight = !mstate.RA_isLeft && mstate.DEC_isLeft
 && current_g.x < 0
 || (mstate.RA_isLeft && !mstate.DEC_isLeft && current_g.x > 0);
 bool headingBackLeft = !mstate.RA_isLeft && mstate.DEC_isLeft
 && current_g.x > 0
 || (mstate.RA_isLeft && !mstate.DEC_isLeft && current_g.x < 0);

 float currentX = current_g.x * (RA_sameTilt ? 1 : -1); // g.x, after RA transition

 println(currentX, 6);
 println(current_g.y, 6);

 DEC_dir = (RA_willBeLeft ? 1 : -1)
 * ((target.x < currentX) && DEC_sameTilt ? -1 : 1)
 * (headingBackward ? -1 : 1);

 if (!DEC_sameTilt && headingBackLeft) {
 DEC_dir = CW;
 }
 if (!DEC_sameTilt && headingBackRight) {
 DEC_dir = CCW;
 }
 */
#ifdef calcDirections_log
printf(
		"mstate.RA_isLeft=%d mstate.DEC_isLeft=%d DEC_willBeLeft=%d RA_willBeLeft=%d "
		"DEC_sameTilt=%d RA_sameTilt=%d headingBackRight=%d headingBackLeft=%d headingBackward=%d currentXY=",
		mstate.RA_isLeft, mstate.DEC_isLeft, DEC_willBeLeft, RA_willBeLeft, DEC_sameTilt,
		RA_sameTilt, headingBackRight, headingBackLeft, headingBackward);

printf("DEC_dir=%s RA_dir=%s\n", (DEC_dir == CW) ? "CW" : "CCW", (RA_dir == CW) ? "CW" : "CCW");
#endif
}

// set current orientation
void setOrientation(triple_t &pos) {
setCurrentGravity(pos);
pos.RA_isLeft = current_mstate.RA_isLeft;
}

bool DEC_done = false, RA_done = false, cancelAll;
float lastDeltaZ = 2, lastDeltaX = 2;
#define ERROR_LIMIT 7
#define X_ERROR  0.0005
#define Z_ERROR  X_ERROR

#define DELTA_X_ERROR  0.0001 // maximum acceptable deviation to a wrong direction
#define DELTA_Z_ERROR  DELTA_X_ERROR
long xCounter = ERROR_LIMIT, zCounter = ERROR_LIMIT;

bool moveToTarget(const triple_t& gravity, const triple_t& target) {

float deltaZ, deltaX;

deltaX = abs(gravity.x - target.x); //target.x;
if (deltaX - lastDeltaX > DELTA_X_ERROR) {
	xCounter--;
} else { // reset the counter
	xCounter = min(ERROR_LIMIT, xCounter + 1);
}

/*
 printf("%d ", xCounter);
 print(gravity->x, 6);
 print("  ");
 print(deltaX - lastDeltaX, 4);
 */
lastDeltaX = deltaX;

if (deltaX < X_ERROR) {
	stepper2.stop();
	//stepper2.disableOutputs();
	if (!DEC_done) {
		printf("DEC on target! deltaXZ=");
		print(deltaX, 6);
		print("  ");
		println(deltaZ, 6);
		DEC_done = true;
		//RA_done = false;
	}

} else {
	DEC_done = false;
	if (xCounter == 0 /*|| DEC_limit(gravity, DEC_dir)*/) {
		println("DEC going wrong direction!");
		stepper2.stop();
		calcDirections(target);
		//DEC_dir *= -1;
		xCounter = ERROR_LIMIT; // give it enough time for changing direction
	}
	//DEC_done = false;
	setSPS(1, max(50000 * abs(deltaX), 80));
	stepper2.move(DEC_dir * max(20000 * abs(deltaX), 1000));

}

deltaZ = abs(gravity.z - target.z);
if (target.y * gravity.y < 0) { // then RA must flip
	deltaZ = 2 - deltaZ;
}
if (deltaZ - lastDeltaZ > DELTA_Z_ERROR) { // when to flip direction
	zCounter--;
} else {
	zCounter = min(ERROR_LIMIT, zCounter + 1);
}

/*
 printf("	%d ", zCounter);
 print(gravity->z, 6);
 print("  ");
 println(deltaZ - lastDeltaZ, 4);
 */
lastDeltaZ = deltaZ;

if (deltaZ < Z_ERROR) {
	stepper1.stop();
//		stepper1.disableOutputs();
	if (!RA_done) {
		printf("RA on target! deltaXZ=");
		print(deltaX, 6);
		print("  ");
		println(deltaZ, 6);
		RA_done = true;
		//DEC_done = false;
	}

} else {
	RA_done = false;
	if (zCounter == 0 /*|| RA_limit(gravity, RA_dir)*/) {
		println("RA going wrong direction!");
		calcDirections(target);
		//RA_dir *= -1;
		zCounter = ERROR_LIMIT; // give it enough time for changing direction
	}
	setSPS(0, max(50000 * abs(deltaZ), 100));
	stepper1.move(RA_dir * max(20000 * abs(deltaZ), 1000));
}
/*print("RA speed=");
 print(max(50000 * abs(deltaZ), 100));
 print(" RA dist=");
 print(max(20000 * abs(deltaZ), 10));
 print(" DEC speed=");
 print(max(min(8000, 50000 * abs(deltaX)), 100));
 print(" DEC dist=");
 println(max(20000 * abs(deltaX), 10));*/

return !DEC_done || !RA_done;

}

int RA_FPC = 0; // false positive counter
int DEC_FPC = 0; // false positive counter
void update_RA_state(const triple_t& g1, triple_t& g2, mount_state_t &mstate,
	int direction, const int maxCount) {

bool isLeftTilted = (abs(g2.z - g1.z) < 0.004) ? mstate.RA_isLeft : // if the change is too small then forget it
		((g2.z < g1.z) == (direction == CCW));

if (mstate.RA_isLeft != isLeftTilted) {
	if (RA_FPC-- == 0) {
		mstate.RA_isLeft = isLeftTilted;
		RA_FPC = maxCount; // reset for the next occasion

		print(direction);
		print(mstate.RA_isLeft);
		print(RA_FPC);
		println(g2.z - g1.z, 6);

	}
} else {
	RA_FPC = maxCount; // reset if the change was undone
}
//println(g2.z - g1.z, 6);
g2.RA_isLeft = mstate.RA_isLeft;
}
void update_RA_state(const triple_t& g1, triple_t& g2, mount_state_t &mstate,
	int direction) {
update_RA_state(g1, g2, mstate, direction, 0);
}
void update_DEC_state(const triple_t& g1, const triple_t& g2,
	mount_state_t &mstate, int direction, const int maxCount) {
bool isLeftTilted = (abs(g2.x - g1.x) < 0.0001) ? mstate.DEC_isLeft : // if the change is too small then forget it
		((abs(g2.x) > abs(g1.x)) == (direction == CCW));

if (mstate.DEC_isLeft != isLeftTilted) {
	if (DEC_FPC-- == 0) {
		mstate.DEC_isLeft = isLeftTilted;
		DEC_FPC = maxCount;

		print(direction);
		print(mstate.DEC_isLeft);
		println(g2.x - g1.x, 6);
	}
} else {
	DEC_FPC = maxCount;
}
/*
 print(direction == CCW ? "CCW" : "CW");
 print(" g2.x-g1.x= ");
 println(g2.x - g1.x, 6);
 */
}

void update_DEC_state(const triple_t& g1, const triple_t& g2,
	mount_state_t &mstate, int direction) {
update_DEC_state(g1, g2, mstate, direction, 0);
}

void evaluate_RA(triple_t& g1, mount_state_t &mstate) {
mstate.RA_speed = 1;
const int TEST_STEPS = stepModes[sm] * 300;
int direction;
stepper1.enableOutputs();
triple_t g2;
if (g1.z > 0.99) {
	stepper1.move(TEST_STEPS);
	while (stepper1.run()) {
		loop();
	}
}
setSPS(0, stepModes[sm] * 200);
for (int i = 0; i < 2; i++) {
	direction = (i % 2 ? CW : CCW);
	stepper1.move(direction * TEST_STEPS / 2);
	stepper1.runToPosition();
	setCurrentGravity(g2);
	update_RA_state(g1, g2, mstate, direction);
	mstate.RA_speed = min(mstate.RA_speed,
			abs(acos(g2.z)-acos(g1.z)) / TEST_STEPS * 2);
	g1.x = g2.x;
	g1.y = g2.y;
	g1.z = g2.z;
}
i = 1;
while (mstate.RA_speed <= 0.00001 && i-- > 0 && !cancelAll) { // if it failed then try again
	print("incorrect RA_speed: ");
	println(mstate.RA_speed, 10);
	evaluate_RA(g1, mstate);
}
if (mstate.RA_speed <= 0.000025) {
	mstate.RA_speed = 0.000028;
}
stepper1.disableOutputs();
}

void evaluate_DEC(triple_t& g1, mount_state_t &mstate) {
stepper2.enableOutputs();
mstate.DEC_speed = 1;
const int TEST_STEPS = stepModes[sm] * 200;
int direction;
triple_t g2;
int rounds[4] = { CW, CCW, CCW, CW };
setSPS(1, TEST_STEPS);
for (int i = 0; i < 2; i++) {
	direction = rounds[i];
	stepper2.move(direction * TEST_STEPS / 2);
	stepper2.runToPosition();
	delay(30);
	setOrientation(g2);
	update_DEC_state(g1, g2, mstate, direction);
	mstate.DEC_speed = min(mstate.DEC_speed,
			abs(atan(g2.x/g2.y)-atan(g1.x/g1.y)) / TEST_STEPS * 2);
	g1.x = g2.x;
	g1.y = g2.y;
	g1.z = g2.z;
	if (mstate.DEC_speed <= 0.000001) {
		mstate.DEC_speed = 0.000055;
	}
}

i = 1;
while (mstate.DEC_speed <= 0.00001 && i-- > 0 && !cancelAll) { // if it failed then try again
	print("incorrect DEC_speed: ");
	println(mstate.DEC_speed, 6);
	evaluate_DEC(g1, mstate);
}
stepper2.disableOutputs();
}

void init_drive() {
enableMPU();
setCurrentGravity(current_g);
evaluate_RA(current_g, current_mstate);
//disableMPU();
printf("RA_isLeft=%s RA_speed=", current_mstate.RA_isLeft ? "yes" : "no");
println(current_mstate.RA_speed, 6);
evaluate_DEC(current_g, current_mstate);
printf("DEC_isLeft=%s DEC_speed=", current_mstate.DEC_isLeft ? "yes" : "no");
println(current_mstate.DEC_speed, 6);
}

void waitForUser(String msg) {
// wait for ready
if (msg.length() > 0) {
	println(msg);
} else {
	println(F("Send any character to begin fine tuning..."));
}
while (Serial.available() && Serial.read()
		|| Serial1.available() && Serial1.read())
	; // empty buffer
while (!Serial.available() && !Serial1.available())
	; // wait for data
while (Serial.available() && Serial.read()
		|| Serial1.available() && Serial1.read())
	; // empty buffer again
}

void setup() {
// fill in the UART file descriptor with pointer to writer.
fdev_setup_stream(&uartout, uart_putchar, NULL, _FDEV_SETUP_WRITE);
// The uart is the standard output device STDOUT.
stdout = &uartout;

// initialize serial communication
Serial.begin(9600);
Serial1.begin(9600);

setupMPU();

steppers[0] = &stepper1;
steppers[1] = &stepper2;
sps[0] = sps[1] = 1;
dir[0] = dir[1] = 3;
stepper1.setPinsInverted(0, 0, 1);
stepper1.setEnablePin(ENABLE1);
stepper1.setAcceleration(ACCEL);
stepper1.move(DISTANCE);
stepper1.setMaxSpeed(0);
stepper1.setMaxSpeed(sps[0]);

stepper2.setPinsInverted(0, 0, 1);
stepper2.setEnablePin(ENABLE2);
stepper2.setAcceleration(ACCEL);
stepper2.move(DISTANCE);
stepper2.setMaxSpeed(0);
stepper2.setMaxSpeed(sps[1]);

pinMode(RESET1, OUTPUT); // RESET
pinMode(SLEEP1, OUTPUT); // SLEEP
pinMode(M10, OUTPUT);
pinMode(M11, OUTPUT);
pinMode(M12, OUTPUT);
digitalWrite(RESET1, H);
digitalWrite(SLEEP1, H);

pinMode(RESET2, OUTPUT); // RESET
pinMode(SLEEP2, OUTPUT); // SLEEP
pinMode(M20, OUTPUT);
pinMode(M21, OUTPUT);
pinMode(M22, OUTPUT);
digitalWrite(RESET2, H);
digitalWrite(SLEEP2, H);
digitalWrite(STEP2, L);

sm = 3;
setMicrostep(stepModes[sm], 0);
setMicrostep(stepModes[sm], 1);

loopDuration = estimateLoopTime();
maxSPS = 1000000 / loopDuration;
printf("loopDuration=%1d\t", loopDuration);
printf("maxSPS=%1d", maxSPS);
}

bool runCommand() {
cancelAll = false;
bool isCommand = true;
if (inputString == "1") {
	i = 0;
	println("switched to stepper 1");

} else if (inputString == "2") {
	i = 1;
	println("switched to stepper 2");

} else if (inputString == "3" && !decelerating[i]) {
	steppers[i]->stop();
	decelerating[i] = 1;
	while (steppers[i]->run()) {
		loop(); // let the world keep running
	}
	decelerating[i] = 0;
	if (dir[i] == 4 || dir[i] == 0) {
		dir[i] = 3;
	} else if (dir[i] == 3) {
		printf("motor %1d paused\n", i);
		dir[i] = 0;
	}
	if (dir[i] == 3) { // keep the previous direction
		steppers[i]->move(-DISTANCE * stepModes[sm]); // CW
	}

} else if (inputString == "4" && !decelerating[i]) {
	steppers[i]->stop();
	while (steppers[i]->run()) {
		loop(); // let the world keep running
	}
	decelerating[i] = 0;
	if (dir[i] == 3 || dir[i] == 0) {
		dir[i] = 4;
	} else if (dir[i] == 4) {
		printf("motor %1d paused\n", i);
		dir[i] = 0;
	}
	if (dir[i] == 4) { // keep the previous direction
		steppers[i]->move(DISTANCE * stepModes[sm]); // CCW
	}

} else if (inputString == "5") {
	setSPS(i, getSpeed(i) - 5 * stepModes[sm]);
	printf("sps%1d=%1d\n", i, sps[i]);

} else if (inputString == "6") {
	setSPS(i, getSpeed(i) + 5 * stepModes[sm]);
	printf("sps%1d=%1d\n", i, sps[i]);

} else if (inputString == "7") {
	if (sm > 0)
		sm--;
	setMicrostep(stepModes[sm], i);
	printf("mode%1d=%1d\n", i, stepModes[sm]);

} else if (inputString == "8") {
	if (sm < 5)
		sm++;
	setMicrostep(stepModes[sm], i);
	printf("mode%1d=%1d\n", i, stepModes[sm]);

} else if (inputString == "9") {
	steppers[i]->enableOutputs();
	printf("motor %1d enabled.\n", i);

} else if (inputString == "0") {
	steppers[i]->disableOutputs();
	printf("motor %1d disabled.\n", i);

} else if (inputString == "q" && getMPULock()) {
	enableMPU();
	setYPR(current_g);
	disableMPU();
	print("Z_angle=");
	print(degrees(atan(current_g.z / current_g.y)), 4);
	print(" X_angle=");
	print(degrees(atan(current_g.x / current_g.y)), 4);
	print("  ");
	printGravity(current_g);

} else if (inputString == "w") {
	stepper2.move(8);
} else if (inputString == "s") {
	stepper2.move(-8);
} else if (inputString == "a") {
	stepper1.move(-8);
} else if (inputString == "d") {
	stepper1.move(8);
} else if (inputString == "W") {
	stepper2.move(50);
} else if (inputString == "S") {
	stepper2.move(-50);
} else if (inputString == "A") {
	stepper1.move(-50);
} else if (inputString == "D") {
	stepper1.move(50);

} else if (inputString == "l") {
	printf("\nmotorRA speed=%d, motorDEC=%d", stepper1.speed(),
			stepper2.speed());

} else if (inputString == "p" && getMPULock()) {
	enableMPU();
	init_drive();
	mount_state_t &mstate = current_mstate;

	RA_done = DEC_done = false;

	triple_t target;
	setOrientation(target);
	printGravity(target);

	if (RA_limit(target, RA_dir, mstate)) { // check if target is reachable
		cancelAll = true;
		println(">>>>>>>>>>>>>>> Target not reachable.");
	}

	waitForUser(
			"Move the RA and DEC away from home positions then press any key.");
	init_drive(); // update mount state
	setOrientation(current_g);

	calcDirections(target);

	stepper1.enableOutputs();
	stepper2.enableOutputs();

	stepper1.move(RA_dir * (getDistance_RA(current_g, target, mstate) * 0.9));
	printf("DEC_dir=%s RA_dir=%s\n", (DEC_dir == CW) ? "CW" : "CCW",
			(RA_dir == CW) ? "CW" : "CCW");
	print("stepper1.move=");
	println(getDistance_RA(current_g, target, mstate));

	stepper2.move(
			DEC_dir
					* (getDistance_DEC(current_g, target, mstate, DEC_dir) * 0.9));
	print("stepper2.move=");
	println(getDistance_DEC(current_g, target, mstate, DEC_dir));

	setSPS(0, stepModes[0] * 200);
	setSPS(1, stepModes[1] * 200);
	triple_t g1;
	while ((stepper1.run() || stepper2.run() || !loopMPU(false)) && !cancelAll) {
		loop();
		setCurrentGravity(current_g, true);
		update_RA_state(g1, current_g, current_mstate, RA_dir);
		update_DEC_state(g1, current_g, current_mstate, DEC_dir);

	}

	printGravity(current_g);

	calcDirections(target); // we could have passed target slightly, therefore re-calculate directions
	printf("DEC_dir1=%s RA_dir1=%s\n", (DEC_dir == CW) ? "CW" : "CCW",
			(RA_dir == CW) ? "CW" : "CCW");

	waitForUser("");

	while (moveToTarget(current_g, target) && !cancelAll) {
		getGravity(g1); // get the old values
		setCurrentGravity(current_g, true); // update the values
		update_RA_state(g1, current_g, current_mstate, RA_dir);
		update_DEC_state(g1, current_g, current_mstate, DEC_dir);
		//delay(600);
	}
	disableMPU();
	printGravity(target, "target: ");
	printGravity(current_g, "current: ");
	print("errors=");
	print(target.x - current_g.x, 6);
	print(" ");
	print(target.y - current_g.y, 6);
	print(" ");
	print(target.z - current_g.z, 6);

	stepper1.stop();
	stepper2.stop();

} else if (inputString == "o") {
	stepper1.disableOutputs();
	stepper2.disableOutputs();
	stepper1.stop();
	stepper2.stop();
	cancelAll = true;

} else if (inputString == "i") {
	init_drive();
} else {
	isCommand = false;
}
return isCommand;
}

void loop() {
// read from port 1, send to port 0:
if (Serial1.available()) {
	/*int inByte = Serial1.read();
	 Serial.write(inByte);*/
	while (Serial1.available()) {
		char inChar = (char) Serial1.read(); //read the input
		inputString += inChar; //make a string of the characters coming on serial1
	}

	while (Serial.available() > 0) {
		junk = Serial.read();
	} // clear the serial buffer

	if (!runCommand()) {
		println(inputString);
	}
	inputString = "";
}

if (Serial.available()) {
	while (Serial.available()) {
		char inChar = (char) Serial.read(); //read the input
		inputString += inChar; //make a string of the characters coming on serial
	}

	while (Serial.available() > 0) {
		junk = Serial.read();
	} // clear the serial buffer

	if (!runCommand()) {
		println(inputString);
	}
	inputString = "";
}
stepper1.run();
stepper2.run();
}

// A very simple implementation of the MPU-6050 limited to the 
// TWANG requirements
// I reused the function names to make it compatible
// B. Dring 2/2018

class Twang_MPU
{

  public:
	  void initialize();
	  void getMotion6(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel, int16_t* xGyro, int16_t* yGyro, int16_t* zGyro);
	  bool verify();

  private:
	  static const uint8_t MPU_ADDR = 0x68;
	  static const uint8_t PWR_MGMT_1 = 0x6B;
	  static const uint8_t MPU_DATA_REG_START = 0x3B;
	  static const uint8_t MPU_DATA_LEN = 14;	
		static const uint8_t MPU_DATA_WHO_AM_I = 0x75;
		
};

void Twang_MPU::initialize()
{
	Wire.beginTransmission(MPU_ADDR);
	Wire.write(PWR_MGMT_1);  // PWR_MGMT_1 register
	Wire.write(1);     // wakes up the MPU-6050
	Wire.endTransmission(true);
}

bool Twang_MPU::verify()
{
	Wire.beginTransmission(MPU_ADDR);
	Wire.write(MPU_DATA_WHO_AM_I);  
	Wire.endTransmission(false);
	Wire.requestFrom(MPU_ADDR,1,true);  // read the whole MPU data section
	return (Wire.read() == MPU_ADDR);
}

void Twang_MPU::getMotion6(int16_t* xAccel, int16_t* yAccel, int16_t* zAccel, int16_t* xGyro, int16_t* yGyro, int16_t* zGyro)
{
	
	Wire.beginTransmission(MPU_ADDR);
	Wire.write(MPU_DATA_REG_START);  // starting with register 0x3B (ACCEL_XOUT_H)
	Wire.endTransmission(false);
	Wire.requestFrom(MPU_ADDR,MPU_DATA_LEN,true);  // read the whole MPU data section
	*xAccel=Wire.read()<<8|Wire.read();  // x Accel    
	*yAccel=Wire.read()<<8|Wire.read();  // y Accel
	*zAccel=Wire.read()<<8|Wire.read();  // z Accel
	Wire.read(); Wire.read();  // Temperature..not used, but need to read it
	*xGyro=Wire.read()<<8|Wire.read();  // x Gyro
	*yGyro=Wire.read()<<8|Wire.read();  // y Gyro
	*zGyro=Wire.read()<<8|Wire.read();  // z Gyro
}


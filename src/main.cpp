#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <chrono>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>


using namespace ::std;


class I2CBus
{
  public:
    I2CBus(unsigned _busId)
     :m_busFd(-1),
      m_busPath()
    {
      ostringstream busDevName;
      busDevName << "/dev/i2c-" << _busId;
      m_busPath = busDevName.str();
      m_busFd = ::open(m_busPath.c_str(), O_RDWR | O_SYNC);
      if (m_busFd == -1)
      {
        ostringstream os;
        os << "cannot open i2c bus device " << m_busPath << ", error " << errno;
        throw runtime_error(os.str());
      }
    }

    ~I2CBus()
    {
      if (m_busFd != -1)
        ::close(m_busFd);
    }

    int           fd()   const { return m_busFd; }
    const string& path() const { return m_busPath; }

  private:
    int    m_busFd;
    string m_busPath;
};


class I2CDevice
{
  public:
    I2CDevice(unsigned _busId, unsigned _deviceId)
     :m_i2cBus(_busId)
    {
      if (ioctl(m_i2cBus.fd(), I2C_SLAVE, _deviceId) == -1)
      {
        ostringstream os;
        os << "cannot select i2c slave device " << _deviceId << ", bus " << m_i2cBus.path() << ", error " << errno;
        throw runtime_error(os.str());
      }
    }

    int smbusAccess(uint8_t _readWrite, uint8_t _cmd, i2c_smbus_data& _data, size_t _bytes)
    {
      struct i2c_smbus_ioctl_data args;
      args.read_write = _readWrite;
      args.command = _cmd;
      args.size = _bytes;
      args.data = &_data;
      return ioctl(m_i2cBus.fd(), I2C_SMBUS, &args);
    }

  private:
    I2CBus m_i2cBus;
};


class MSPControl
{
  public:
    MSPControl(unsigned _busId, unsigned _deviceId)
     :m_i2cDevice(_busId, _deviceId)
    {
    }

    unsigned readWord(unsigned _addr)
    {
      i2c_smbus_data i2cData;
      int res = m_i2cDevice.smbusAccess(I2C_SMBUS_READ, _addr, i2cData, I2C_SMBUS_WORD_DATA);
      if (res != 0)
      {
        ostringstream os;
        os << "failed ioctl(SMBUS_READ)";
        throw runtime_error(os.str());
      }

      return i2cData.word;
    }

  private:
    I2CDevice m_i2cDevice;
};


class GPIOControl
{
  public:
    GPIOControl(unsigned _gpio)
     :m_gpioFd(-1),
      m_gpioPath()
    {
      ostringstream gpioCtrlName;
      gpioCtrlName << "/sys/class/gpio/gpio" << _gpio << "/value";
      m_gpioPath = gpioCtrlName.str();
      m_gpioFd = ::open(m_gpioPath.c_str(), O_RDWR | O_SYNC);
      if (m_gpioFd == -1)
      {
        ostringstream os;
        os << "cannot open gpio control " << m_gpioPath;
        throw runtime_error(os.str());
      }
    }

    ~GPIOControl()
    {
      if (m_gpioFd != -1)
        ::close(m_gpioFd);
    }

    void setValue(unsigned _val)
    {
      ostringstream valueStream;
      valueStream << _val << endl;
      const string& value = valueStream.str();
      if (::write(m_gpioFd, value.c_str(), value.length()) != value.length())
      {
        ostringstream os;
        os << "cannot set gpio value " << m_gpioPath;
        throw runtime_error(os.str());
      }
    }

    const string& path() const { return m_gpioPath; }

  private:
    int m_gpioFd;
    string m_gpioPath;
};


class TrikCoilGun
{
  public:
    TrikCoilGun(unsigned _mspBusId, unsigned _mspDeviceId,
                unsigned _mspChargeLevelCmd, unsigned _mspDischargeCurrentCmd,
                unsigned _gpioChargeControl, unsigned _gpioDischargeControl)
     :m_mspControl(_mspBusId, _mspDeviceId),
      m_mspCmdChargeLevel(_mspChargeLevelCmd),
      m_mspCmdDischargeCurrent(_mspDischargeCurrentCmd),
      m_gpioChargeControl(_gpioChargeControl),
      m_gpioDischargeControl(_gpioDischargeControl)
    {
      m_gpioChargeControl.setValue(0);
      m_gpioDischargeControl.setValue(0);
    }

    ~TrikCoilGun()
    {
      m_gpioChargeControl.setValue(0);
      m_gpioDischargeControl.setValue(0);
    }

    void charge(unsigned _durationMs, unsigned _chargeLevel)
    {
      const chrono::steady_clock::time_point& startAt = chrono::steady_clock::now();
      const bool waitCharge = _durationMs == 0;
      const chrono::steady_clock::time_point& elapseAt = startAt + chrono::duration<chrono::steady_clock::rep, chrono::milliseconds::period>(_durationMs);

      cerr << "Preparing for charge to level " << _chargeLevel << endl;
      bool charging = false;

      while (true)
      {
        if (!waitCharge && chrono::steady_clock::now() >= elapseAt)
          break;

        const unsigned currentChargeLevel = m_mspControl.readWord(m_mspCmdChargeLevel);
        if (currentChargeLevel >= _chargeLevel)
        {
          if (charging)
            cerr << "Stop charging at level " << currentChargeLevel << ", target level " << _chargeLevel << endl;
          charging = false;

          if (waitCharge)
            break;
          m_gpioChargeControl.setValue(0);
        }
        else
        {
          if (!charging)
            cerr << "Charging at level " << currentChargeLevel << ", target level " << _chargeLevel << endl;
          charging = true;
          m_gpioChargeControl.setValue(1);
        }
        usleep(1000);
      }

      cerr << "Charge done" << endl;
      m_gpioChargeControl.setValue(0);
    }


    void discharge(unsigned _durationMs, unsigned _zeroChargeLevel)
    {
      const chrono::steady_clock::time_point& startAt = chrono::steady_clock::now();
      const bool waitDischarge = _durationMs == 0;
      const chrono::steady_clock::time_point& elapseAt = startAt + chrono::duration<chrono::steady_clock::rep, chrono::milliseconds::period>(_durationMs);

      cerr << "Preparing for discharge from level " << m_mspControl.readWord(m_mspCmdChargeLevel)
           << " to level " << _zeroChargeLevel << endl;
      while (true)
      {
        if (!waitDischarge && chrono::steady_clock::now() >= elapseAt)
          break;

        const unsigned currentChargeLevel = m_mspControl.readWord(m_mspCmdChargeLevel);
        if (currentChargeLevel <= _zeroChargeLevel)
        {
          cerr << "Discharged to level " << currentChargeLevel << ", target level " << _zeroChargeLevel << endl;
          break;
        }

        m_gpioDischargeControl.setValue(1);
        usleep(1000);
      }

      cerr << "Discharge done, current level " << m_mspControl.readWord(m_mspCmdChargeLevel)
           << ", target level " << _zeroChargeLevel << endl;
      m_gpioDischargeControl.setValue(0);
    }


    void fire(unsigned _preDelayMs, unsigned _durationMs, unsigned _postDelayMs)
    {
      m_gpioChargeControl.setValue(0);
      usleep(_preDelayMs * 1000);

      cerr << "Fire!" << endl;
      m_gpioDischargeControl.setValue(1);
      usleep(_durationMs * 1000);
      m_gpioDischargeControl.setValue(0);
      cerr << "Fire done" << endl;

      usleep(_postDelayMs * 1000);
    }


  private:
    MSPControl m_mspControl;
    unsigned m_mspCmdChargeLevel;
    unsigned m_mspCmdDischargeCurrent;
    GPIOControl m_gpioChargeControl;
    GPIOControl m_gpioDischargeControl;
};





int printUsageHelp()
{
#warning TODO
  return 1;
}

int main(int _argc, char* const _argv[])
{
  static const char* s_optstring = "h";
  static const struct option s_longopts[] = {
    { "help",				no_argument,		NULL,	0},
    { "msp-i2c-bus",			required_argument,	NULL,	0}, // 1
    { "msp-i2c-device",			required_argument,	NULL,	0},
    { "msp-i2c-charge-level",		required_argument,	NULL,	0},
    { "msp-i2c-discharge-current",	required_argument,	NULL,	0},
    { "gpio-charge",			required_argument,	NULL,	0}, // 5
    { "gpio-discharge",			required_argument,	NULL,	0},
    { "charge-duration",		required_argument,	NULL,	0}, // 7
    { "charge-level",			required_argument,	NULL,	0},
    { "fire-predelay",			required_argument,	NULL,	0}, // 9
    { "fire-duration",			required_argument,	NULL,	0},
    { "fire-postdelay",			required_argument,	NULL,	0},
    { "discharge-duration",		required_argument,	NULL,	0}, // 12
    { "discharge-level",		required_argument,	NULL,	0},
    { NULL,				0,			NULL,	0},
  };

  int longopt;
  int opt;

  unsigned mspI2cBusId = 0x2;
  unsigned mspI2cDeviceId = 0x48;
  unsigned mspChargeLevelCmd = 0x25;
  unsigned mspDischargeCurrentCmd = 0x24;
  unsigned gpioChargeCtrl = 0x17;
  unsigned gpioDischargeCtrl = 0x00;

  unsigned chargeDurationMs = 0;
  unsigned chargeLevel = 0x200;
  unsigned firePreDelayMs = 10;
  unsigned fireDurationMs = 10;
  unsigned firePostDelayMs = 100;
  unsigned dischargeDurationMs = 0;
  unsigned dischargeLevel = 0x5;

  while ((opt = getopt_long(_argc, _argv, s_optstring, s_longopts, &longopt)) != -1)
  {
    switch (opt)
    {
      case 'h':		return printUsageHelp();

      case 0:
        switch (longopt)
        {
          case 0:	return printUsageHelp();

          case 1:	mspI2cBusId		= atoi(optarg);	break;
          case 2:	mspI2cDeviceId		= atoi(optarg);	break;
          case 3:	mspChargeLevelCmd	= atoi(optarg);	break;
          case 4:	mspDischargeCurrentCmd	= atoi(optarg);	break;
          case 5:	gpioChargeCtrl		= atoi(optarg);	break;
          case 6:	gpioDischargeCtrl	= atoi(optarg);	break;

          case 7:	chargeDurationMs	= atoi(optarg);	break;
          case 8:	chargeLevel		= atoi(optarg);	break;
          case 9:	firePreDelayMs		= atoi(optarg);	break;
          case 10:	fireDurationMs		= atoi(optarg);	break;
          case 11:	firePostDelayMs		= atoi(optarg);	break;
          case 12:	dischargeDurationMs	= atoi(optarg);	break;
          case 13:	dischargeLevel		= atoi(optarg);	break;
          default:	return printUsageHelp();
        }
        break;
      default:		return printUsageHelp();
    }
  }

  cout << "Charge duration " << chargeDurationMs << "ms, level " << chargeLevel << endl;
  cout << "Fire pre-delay " << firePreDelayMs << "ms, duration " << fireDurationMs << "ms, post-delay " << firePostDelayMs << "ms" << endl;
  cout << "Discharge duration " << dischargeDurationMs << "ms, level " << dischargeLevel << endl;

  TrikCoilGun coilGun(mspI2cBusId, mspI2cDeviceId, mspChargeLevelCmd, mspDischargeCurrentCmd, gpioChargeCtrl, gpioDischargeCtrl);

  coilGun.charge(chargeDurationMs, chargeLevel);
  coilGun.fire(firePreDelayMs, fireDurationMs, firePostDelayMs);
  coilGun.discharge(dischargeDurationMs, dischargeLevel);
}


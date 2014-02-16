#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <chrono>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
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
        os << "cannot open i2c bus device " << m_busPath;
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
      if (ioctl(m_i2cBus.fd(), I2C_SLAVE, &_deviceId) != 0)
      {
        ostringstream os;
        os << "cannot select i2c slave device " << _deviceId << ", bus " << m_i2cBus.path();
        throw runtime_error(os.str());
      }
    }

    template <typename UInt, size_t _bytes = sizeof(UInt)>
    bool readUInt(UInt& _data)
    {
      uint8_t dataBuf[_bytes];
      if (::read(m_i2cBus.fd(), dataBuf, sizeof(dataBuf)) != sizeof(dataBuf))
        return false;

      _data = UInt();
      for (size_t idx = 0; idx < _bytes; ++idx)
        _data |= static_cast<UInt>(dataBuf[idx]) << (idx*8);
      return true;
    }

    template <typename UInt, size_t _bytes = sizeof(UInt)>
    bool writeUInt(const UInt& _data)
    {
      uint8_t dataBuf[_bytes];
      for (size_t idx = 0; idx < _bytes; ++idx)
        dataBuf[idx] = static_cast<uint8_t>(_data >> (idx*8));

      if (::write(m_i2cBus.fd(), dataBuf, sizeof(dataBuf)) != sizeof(dataBuf))
        return false;
      return true;
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
      uint8_t addr = static_cast<uint8_t>(_addr);
      if (!m_i2cDevice.writeUInt(addr))
      {
        ostringstream os;
        os << "cannot write cmd";
        throw runtime_error(os.str());
      }

      uint16_t result;
      if (!m_i2cDevice.readUInt(result))
      {
        ostringstream os;
        os << "cannot read result";
        throw runtime_error(os.str());
      }

      return result;
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

#if 1
      cerr << "Preparing for charge" << endl;
      bool charging = false;
#endif
      while (true)
      {
        if (!waitCharge && chrono::steady_clock::now() >= elapseAt)
          break;

        if (m_mspControl.readWord(m_mspCmdChargeLevel) >= _chargeLevel)
        {
#if 1
          if (charging)
            cerr << "Stop charging" << endl;
          charging = false;
#endif
          if (waitCharge)
            break;
          m_gpioChargeControl.setValue(0);
        }
        else
        {
#if 1
          if (!charging)
            cerr << "Charging" << endl;
          charging = true;
#endif
          m_gpioChargeControl.setValue(1);
        }
        usleep(1000);
      }
#if 1
      cerr << "Charge done" << endl;
#endif
      m_gpioChargeControl.setValue(0);
    }


    void discharge(unsigned _durationMs, unsigned _zeroChargeLevel)
    {
      const chrono::steady_clock::time_point& startAt = chrono::steady_clock::now();
      const bool waitDischarge = _durationMs == 0;
      const chrono::steady_clock::time_point& elapseAt = startAt + chrono::duration<chrono::steady_clock::rep, chrono::milliseconds::period>(_durationMs);

#if 1
      cerr << "Preparing for discharge" << endl;
#endif
      while (true)
      {
        if (!waitDischarge && chrono::steady_clock::now() >= elapseAt)
          break;

        if (m_mspControl.readWord(m_mspCmdChargeLevel) <= _zeroChargeLevel)
        {
#if 1
          cerr << "Discharged" << endl;
#endif
          break;
        }

        m_gpioDischargeControl.setValue(1);
        usleep(1000);
      }
#if 1
      cerr << "Discharge done" << endl;
#endif
      m_gpioDischargeControl.setValue(0);
    }


    void fire(unsigned _preDelayMs, unsigned _durationMs, unsigned _postDelayMs)
    {
      m_gpioChargeControl.setValue(0);
      usleep(_preDelayMs * 1000);
      m_gpioDischargeControl.setValue(1);
      usleep(_durationMs * 1000);
      m_gpioChargeControl.setValue(0);
      usleep(_postDelayMs * 1000);
    }


  private:
    MSPControl m_mspControl;
    unsigned m_mspCmdChargeLevel;
    unsigned m_mspCmdDischargeCurrent;
    GPIOControl m_gpioChargeControl;
    GPIOControl m_gpioDischargeControl;
};





#if 0


static int s_chargeLevel = 0;
static int s_chargeDurationMs = 0;
static int s_fireDurationMs = 10;
static int s_dischargeDelayMs = 100;
static int s_dischargeDurationMs = 0;
static int s_dischargeLevel = 0;


#endif


int main()
{
  TrikCoilGun coilGun(0,0,0,0,0,0);

  coilGun.charge(1000, 0);
  coilGun.fire(10, 10, 100);
  coilGun.discharge(1000, 0);
}


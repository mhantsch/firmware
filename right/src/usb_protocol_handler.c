#include "usb_protocol_handler.h"
#include "system_properties.h"
#include "peripherals/test_led.h"
#include "i2c_addresses.h"
#include "peripherals/led_driver.h"
#include "peripherals/merge_sensor.h"
#include "config_parser/parse_config.h"
#include "config_parser/config_globals.h"
#include "led_pwm.h"
#include "slave_scheduler.h"
#include "slave_drivers/uhk_module_driver.h"
#include "slave_drivers/kboot_driver.h"
#include "bootloader/wormhole.h"
#include "peripherals/adc.h"
#include "eeprom.h"
#include "keymap.h"
#include "microseconds/microseconds_pit.c"
#include "i2c_watchdog.h"
#include "usb_commands/usb_command_apply_config.h"
#include "usb_commands/usb_command_read_config.h"

uint8_t UsbDebugInfo[USB_GENERIC_HID_OUT_BUFFER_LENGTH];

// Functions for setting error statuses

void SetUsbError(uint8_t error)
{
    GenericHidOutBuffer[0] = error;
}

void SetUsbResponseByte(uint8_t response)
{
    GenericHidOutBuffer[1] = response;
}

void SetUsbResponseWord(uint16_t response)
{
    *((uint16_t*)(GenericHidOutBuffer+1)) = response;
}

// Per command protocol command handlers

void getSystemProperty(void)
{
    uint8_t propertyId = GenericHidInBuffer[1];

    switch (propertyId) {
        case SystemPropertyId_UsbProtocolVersion:
            SetUsbResponseByte(SYSTEM_PROPERTY_USB_PROTOCOL_VERSION);
            break;
        case SystemPropertyId_BridgeProtocolVersion:
            SetUsbResponseByte(SYSTEM_PROPERTY_BRIDGE_PROTOCOL_VERSION);
            break;
        case SystemPropertyId_DataModelVersion:
            SetUsbResponseByte(SYSTEM_PROPERTY_DATA_MODEL_VERSION);
            break;
        case SystemPropertyId_FirmwareVersion:
            SetUsbResponseByte(SYSTEM_PROPERTY_FIRMWARE_VERSION);
            break;
        case SystemPropertyId_HardwareConfigSize:
            SetUsbResponseWord(HARDWARE_CONFIG_SIZE);
            break;
        case SystemPropertyId_UserConfigSize:
            SetUsbResponseWord(USER_CONFIG_SIZE);
            break;
        default:
            SetUsbError(1);
            break;
    }
}

void reenumerate(void)
{
    Wormhole.magicNumber = WORMHOLE_MAGIC_NUMBER;
    Wormhole.enumerationMode = GenericHidInBuffer[1];
    Wormhole.timeoutMs = *((uint32_t*)(GenericHidInBuffer+2));
    NVIC_SystemReset();
}

void setTestLed(void)
{
    uint8_t ledState = GenericHidInBuffer[1];
    TEST_LED_SET(ledState);
    UhkModuleStates[UhkModuleDriverId_LeftKeyboardHalf].sourceVars.isTestLedOn = ledState;
}

// To be removed. Now it's already part of getKeyboardState()
void readMergeSensor(void)
{
    SetUsbResponseByte(MERGE_SENSOR_IS_MERGED);
}

void setLedPwm(void)
{
    uint8_t brightnessPercent = GenericHidInBuffer[1];
    LedPwm_SetBrightness(brightnessPercent);
    UhkModuleStates[UhkModuleDriverId_LeftKeyboardHalf].sourceVars.ledPwmBrightness = brightnessPercent;
}

void getAdcValue(void)
{
    *(uint32_t*)(GenericHidOutBuffer+1) = ADC_Measure();
}

void legacyLaunchEepromTransfer(void)
{
    uint8_t legacyEepromTransferId = GenericHidInBuffer[1];
    switch (legacyEepromTransferId) {
    case 0:
        EEPROM_LaunchTransfer(EepromOperation_Read, ConfigBufferId_HardwareConfig, NULL);
        break;
    case 1:
        EEPROM_LaunchTransfer(EepromOperation_Write, ConfigBufferId_HardwareConfig, NULL);
        break;
    case 2:
        EEPROM_LaunchTransfer(EepromOperation_Read, ConfigBufferId_ValidatedUserConfig, NULL);
        break;
    case 3:
        EEPROM_LaunchTransfer(EepromOperation_Write, ConfigBufferId_ValidatedUserConfig, NULL);
        break;
    }
}

void writeConfiguration(bool isHardware)
{
    uint8_t length = GenericHidInBuffer[1];
    uint16_t offset = *((uint16_t*)(GenericHidInBuffer+1+1));

    if (length > USB_GENERIC_HID_OUT_BUFFER_LENGTH-1-1-2) {
        SetUsbError(ConfigTransferResponse_LengthTooLarge);
        return;
    }

    uint8_t *buffer = isHardware ? HardwareConfigBuffer.buffer : StagingUserConfigBuffer.buffer;
    uint16_t bufferLength = isHardware ? HARDWARE_CONFIG_SIZE : USER_CONFIG_SIZE;

    if (offset + length > bufferLength) {
        SetUsbError(ConfigTransferResponse_BufferOutOfBounds);
        return;
    }

    memcpy(buffer+offset, GenericHidInBuffer+1+1+2, length);
}

void getKeyboardState(void)
{
    GenericHidOutBuffer[1] = IsEepromBusy;
    GenericHidOutBuffer[2] = MERGE_SENSOR_IS_MERGED;
    GenericHidOutBuffer[3] = UhkModuleStates[UhkModuleDriverId_LeftKeyboardHalf].moduleId;
    GenericHidOutBuffer[4] = UhkModuleStates[UhkModuleDriverId_LeftAddon].moduleId;
    GenericHidOutBuffer[5] = UhkModuleStates[UhkModuleDriverId_RightAddon].moduleId;
}

void getDebugInfo(void)
{
    *(uint32_t*)(UsbDebugInfo+1) = I2C_Watchdog;
    *(uint32_t*)(UsbDebugInfo+5) = I2cSchedulerCounter;
    *(uint32_t*)(UsbDebugInfo+9) = I2cWatchdog_OuterCounter;
    *(uint32_t*)(UsbDebugInfo+13) = I2cWatchdog_InnerCounter;

    memcpy(GenericHidOutBuffer, UsbDebugInfo, USB_GENERIC_HID_OUT_BUFFER_LENGTH);

/*    uint64_t ticks = microseconds_get_ticks();
    uint32_t microseconds = microseconds_convert_to_microseconds(ticks);
    uint32_t milliseconds = microseconds/1000;
    *(uint32_t*)(GenericHidOutBuffer+1) = ticks;
*/
}

void jumpToSlaveBootloader(void)
{
    uint8_t uhkModuleDriverId = GenericHidInBuffer[1];

    if (uhkModuleDriverId >= UHK_MODULE_MAX_COUNT) {
        SetUsbError(JumpToBootloaderError_InvalidModuleDriverId);
        return;
    }

    UhkModuleStates[uhkModuleDriverId].jumpToBootloader = true;
}

void sendKbootCommand(void)
{
    KbootDriverState.phase = 0;
    KbootDriverState.i2cAddress = GenericHidInBuffer[2];
    KbootDriverState.commandType = GenericHidInBuffer[1];
}

// The main protocol handler function

void UsbProtocolHandler(void)
{
    bzero(GenericHidOutBuffer, USB_GENERIC_HID_OUT_BUFFER_LENGTH);
    uint8_t command = GenericHidInBuffer[0];
    switch (command) {
        case UsbCommandId_GetSystemProperty:
            getSystemProperty();
            break;
        case UsbCommandId_Reenumerate:
            reenumerate();
            break;
        case UsbCommandId_SetTestLed:
            setTestLed();
            break;
        case UsbCommandId_WriteLedDriver:
            break;
        case UsbCommandId_ReadMergeSensor:
            readMergeSensor();
            break;
        case UsbCommandId_WriteUserConfiguration:
            writeConfiguration(false);
            break;
        case UsbCommandId_ApplyConfig:
            UsbCommand_ApplyConfig();
            break;
        case UsbCommandId_SetLedPwm:
            setLedPwm();
            break;
        case UsbCommandId_GetAdcValue:
            getAdcValue();
            break;
        case UsbCommandId_LaunchEepromTransfer:
            legacyLaunchEepromTransfer();
            break;
        case UsbCommandId_ReadHardwareConfiguration:
            UsbCommand_ReadConfig(true);
            break;
        case UsbCommandId_WriteHardwareConfiguration:
            writeConfiguration(true);
            break;
        case UsbCommandId_ReadUserConfiguration:
            UsbCommand_ReadConfig(false);
            break;
        case UsbCommandId_GetKeyboardState:
            getKeyboardState();
            break;
        case UsbCommandId_GetDebugInfo:
            getDebugInfo();
            break;
        case UsbCommandId_JumpToSlaveBootloader:
            jumpToSlaveBootloader();
            break;
        case UsbCommandId_SendKbootCommand:
            sendKbootCommand();
            break;
        default:
            break;
    }
}

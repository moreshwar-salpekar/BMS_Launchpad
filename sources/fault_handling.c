/*
 * fault_handling.c
 *
 *  Created on: Apr 7, 2019
 *      Author: Akshay Godase
 *      Description: This file contains all the handling and detection of faults
 *      If fault is detected, steps are like below
 *      1. Message to ECU
 *      2. Contactor off
 *      3. Wait till fault is recovered
 *      4. Once recovered turn on the contactor
 *      5. Message to ECU
 *      6. reset all ISL devices and reinitialize them
 *      7. read Isl data and start normal execution
 */

#include "all_header.h"

volatile SummaryFaults AllFaults[MAX_DEVICES];
volatile ISL_FLAGS *currentFlags;


void fault_isr(void)
{
    Bool is_fault = False;
    is_fault = checkForFault();
    if(is_fault == True)
        handle_fault();
}

SummaryFaults* CheckFaults(Uint8 device){
    ISL_DEVICE* ISLData = GetISLDevices(device);                                                //Get the Current Module
    AllFaults[device].IsFault=False;AllFaults[device].OverCurrent=False;
    AllFaults[device].OpenWire=False;AllFaults[device].UnderVoltage = False;
    AllFaults[device].OverVoltage = False;AllFaults[device].OverTemp = False;AllFaults[device].UnderTemp = False;
    if((ISLData->PAGE2_1.FAULT.OC.all & ~(7<<5))>0){
        AllFaults[device].OpenWire=True;
        AllFaults[device].IsFault=True;
    }
    if((ISLData->PAGE2_1.FAULT.OF.all & ~(7<<5))>0){
        AllFaults[device].OverVoltage=True;
        AllFaults[device].IsFault=True;
    }
    if((ISLData->PAGE2_1.FAULT.UF.all & ~(7<<5))>0){
        AllFaults[device].UnderVoltage=True;
        AllFaults[device].IsFault=True;
    }
    if((ISLData->PAGE2_1.FAULT.OVTF.all)>0){
        AllFaults[device].OverTemp=True;
        AllFaults[device].IsFault=True;
    }
    if (NowCurrent > OverCurrentThreshold)
    {
        AllFaults[device].OverCurrent =True;
        AllFaults[device].IsFault=True;
    }
    return &AllFaults;
}
void FaultLEDOn()
{
    GpioDataRegs.GPASET.bit.GPIO2 = 1;
}

void FaultLEDOff()
{
    GpioDataRegs.GPACLEAR.bit.GPIO2 = 1;
}

void ConfigureFaultSetup()
{
    //GPIO Fault LED Pin
    GPIO_SetupPinMux(FAULT_LED, GPIO_MUX_CPU1, 0);
    GPIO_SetupPinOptions(FAULT_LED, GPIO_OUTPUT, GPIO_PUSHPULL);
}
Bool checkForFault(void)
{
    Bool is_fault = False;
    Uint8 CurrentDevice =0;
    for(;CurrentDevice<NumISLDevices;CurrentDevice++)
    {
        CheckFaults(CurrentDevice);
        if(AllFaults[CurrentDevice].IsFault == True)
            is_fault = True;
    }
    return is_fault;
}

void alert_ecu(Uint8 device)
{
}

#pragma CODE_SECTION(InitializeISLParameters,".bigCode")
void handle_fault(void)
{

    //disable receive callback so that this callback doesn't hinder normal fault handling
    ISL_DisableReceiveCallback();

    AGAIN:

    //step1: notify ECU
    //alert_ecu(device);

    //step2: turn off the contactor
    contactor_off();

#ifdef DEBUG
    uart_string_newline("Fault detected hence turning off the contactor first and waiting for fault recovery\r\n");
#endif

    //step3: wait for fault_recovery
    recover_from_faults();

#ifdef DEBUG
    uart_string("ReCovered! now read back data again and see if fault is there or not?\r\n");
#endif

    //step4:
    GetISLData(NumISLDevices);
    DELAY_MS(100);

    Bool is_fault;
    is_fault = checkForFault();

    if (is_fault == True)
    {
#ifdef DEBUG
        uart_string("Could not recover from fault hence jumping to handle fault routine again...!\r\n");
#endif
        goto AGAIN;
    }
    //step5 turn on contactor and return to normal execution
#ifdef DEBUG
        uart_string("Recovered from fault,turning On contactor.....!\r\n");
#endif
    FaultLEDOff();
    contactor_on();
    ISL_EnableReceiveCallback();
}

void recover_from_faults(void)
{
    Uint8 CurrentDevice;
    for(CurrentDevice=0;CurrentDevice<NumISLDevices;CurrentDevice++)
    {
        CheckFaults(CurrentDevice);
        while(AllFaults[CurrentDevice].IsFault == True)
        {
            clear_all_fault();
            DELAY_S(2);
            ISL_Request(CurrentDevice+1, READ_FAULTS);
            DELAY_S(3);
            CheckFaults(CurrentDevice);                                     //check for fault again
        }
    }
}
void clear_all_fault(void)
{
#ifdef DEBUG
        uart_string("In Fault Mode...!Clear all faults now...!\r\n");
#endif
    Uint8 Reset[8] = {},device;
    for(device = 1;device <= NumISLDevices; device++)
        ISL_WriteRegister(device,2,0x00,Reset);                                                      //reset all the pages one by one
    for(device = 1;device <= NumISLDevices; device++)
        ISL_WriteRegister(device,2,0x01,Reset);                                                      //reset all the pages one by one
    for(device = 1;device <= NumISLDevices; device++)
        ISL_WriteRegister(device,2,0x02,Reset);                                                      //reset all the pages one by one
    for(device = 1;device <= NumISLDevices; device++)
        ISL_WriteRegister(device,2,0x06,Reset);                                                      //reset all the pages one by one
    for(device = 1;device <= NumISLDevices; device++)
        ISL_WriteRegister(device,2,0x04,Reset);                                                      //reset all the pages one by one
}
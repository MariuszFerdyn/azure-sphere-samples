﻿/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

   // This sample C application for Azure Sphere demonstrates Azure IoT SDK C APIs
   // The application uses the Azure IoT SDK C APIs to
   // 1. Use the buttons to trigger sending telemetry to Azure IoT Hub/Central.
   // 2. Use IoT Hub/Device Twin to control an LED.

   // You will need to provide four pieces of information to use this application, all of which are set
   // in the app_manifest.json.
   // 1. The Scope Id for your IoT Central application (set in 'CmdArgs')
   // 2. The Tenant Id obtained from 'azsphere tenant show-selected' (set in 'DeviceAuthentication')
   // 3. The Azure DPS Global endpoint address 'global.azure-devices-provisioning.net'
   //    (set in 'AllowedConnections')
   // 4. The IoT Hub Endpoint address for your IoT Central application (set in 'AllowedConnections')

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>



// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/networking.h>
#include <applibs/gpio.h>
#include <applibs/storage.h>
#include "epoll_timerfd_utilities.h"
#include <applibs/uart.h>


// By default, this sample is targeted at the MT3620 Reference Development Board (RDB).
// This can be changed using the project property "Target Hardware Definition Directory".
// This #include imports the sample_hardware abstraction from that hardware definition.
#include <hw/sample_hardware.h>

#include "epoll_timerfd_utilities.h"

// Azure IoT SDK
#include <iothub_client_core_common.h>
#include <iothub_device_client_ll.h>
#include <iothub_client_options.h>
#include <iothubtransportmqtt.h>
#include <iothub.h>
#include <azure_sphere_provisioning.h>

#include "minmea.h"

#include "i2c.h"
#include "mt3620_avnet_dev.h"



//UART
static int uartFd = -1;
static int epollFd = -1;
uint8_t danewbuforze[2000];
uint8_t danewyjsciowe[500];
int wskaznik;
int poczatek;
int koniec;
int dlugosc;

struct minmea_sentence_rmc frame;

static volatile sig_atomic_t terminationRequired = false;

#include "parson.h" // used to parse Device Twin messages.

//UART
static void UartEventHandler(EventData* eventData)
{
	const size_t receiveBufferSize = 256;
	uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
	ssize_t bytesRead;

	// Read incoming UART data. It is expected behavior that messages may be received in multiple
	// partial chunks.
	bytesRead = read(uartFd, receiveBuffer, receiveBufferSize);
	if (bytesRead < 0) {
		Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	if (bytesRead > 0) {
		// Null terminate the buffer to make it a valid string, and print it
		receiveBuffer[bytesRead] = 0;
		//Log_Debug("UART received %d bytes: '%s'.\n", bytesRead, (char *)receiveBuffer);
		for (int i = 0; i < bytesRead; i++)
			danewbuforze[i + wskaznik] = receiveBuffer[i];
		wskaznik += bytesRead;
	}
	poczatek = 0;
	koniec = 0;
	if (wskaznik > 1500) {
		//Log_Debug("%d GNRMC Data to Parse: '%s'.\n", wskaznik, (char*)danewbuforze);
		for (int i = 0; i < wskaznik - 10; ++i) {
			if ((danewbuforze[i] == '$') && (danewbuforze[i + 1] == 'G') && (danewbuforze[i + 2] == 'N') && (danewbuforze[i + 3]) == 'R' && (danewbuforze[i + 4] == 'M') && (danewbuforze[i + 5] == 'C')) poczatek = i;
			if (poczatek > 0) break;
		}
		for (int i = poczatek; i < wskaznik - 10; ++i) {
			if (danewbuforze[i] == '\n') koniec = i - 1;
			if (koniec > 0) break;
		}
		if ((poczatek > 0) && (koniec > 0)) {
			for (int i = 0; i < (koniec - poczatek); i++)
				danewyjsciowe[i] = danewbuforze[i + poczatek];
			Log_Debug("%d GNRMC Data: '%s'.\n", wskaznik, (char*)danewyjsciowe);
		}
		for (int i = 0; i < wskaznik; i++) danewbuforze[i] = 0;
		wskaznik = 0;

		//Transfor to Human Read
		switch (minmea_sentence_id(danewyjsciowe, false)) {
		case MINMEA_SENTENCE_RMC: {
			if (minmea_parse_rmc(&frame, danewyjsciowe)) {
				Log_Debug("$RMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d\n",
					frame.latitude.value, frame.latitude.scale,
					frame.longitude.value, frame.longitude.scale,
					frame.speed.value, frame.speed.scale);
				Log_Debug("$RMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d\n",
					minmea_rescale(&frame.latitude, 1000),
					minmea_rescale(&frame.longitude, 1000),
					minmea_rescale(&frame.speed, 1000));
				Log_Debug("$RMC floating point degree coordinates and speed: (%f,%f) %f\n",
					minmea_tocoord(&frame.latitude),
					minmea_tocoord(&frame.longitude),
					minmea_tofloat(&frame.speed));
			}
		} break;

		case MINMEA_SENTENCE_GGA: {
			struct minmea_sentence_gga frame;
			if (minmea_parse_gga(&frame, danewyjsciowe)) {
				Log_Debug("$GGA: fix quality: %d\n", frame.fix_quality);
			}
		} break;

		case MINMEA_SENTENCE_GSV: {
			struct minmea_sentence_gsv frame;
			if (minmea_parse_gsv(&frame, danewyjsciowe)) {
				Log_Debug("$GSV: message %d of %d\n", frame.msg_nr, frame.total_msgs);
				Log_Debug("$GSV: sattelites in view: %d\n", frame.total_sats);
				for (int i = 0; i < 4; i++)
					Log_Debug("$GSV: sat nr %d, elevation: %d, azimuth: %d, snr: %d dbm\n",
						frame.sats[i].nr,
						frame.sats[i].elevation,
						frame.sats[i].azimuth,
						frame.sats[i].snr);
			}
		} break;
		}
	}

}

// event handler data structures. Only the event handler field needs to be populated.
static EventData uartEventData = { .eventHandler = &UartEventHandler };


// Azure IoT Hub/Central defines.
#define SCOPEID_LENGTH 20
static char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
									 // app_manifest.json, CmdArgs

static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
	size_t payloadSize, void* userContextCallback);
static void TwinReportBoolState(const char* propertyName, bool propertyValue);
static void ReportStatusCallback(int result, void* context);
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char* getAzureSphereProvisioningResultString(
	AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SendTelemetry(const unsigned char* key, const unsigned char* value);
static void SendGPSPosition(const unsigned char* key, const unsigned char* value);
static void SetupAzureClient(void);

// Function to generate simulated Temperature data/telemetry
static void SendSimulatedTemperature(void);

// Initialization/Cleanup
static int InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);

// File descriptors - initialized to invalid value
// Buttons
static int sendMessageButtonGpioFd = -1;
static int sendOrientationButtonGpioFd = -1;

// LED
static int deviceTwinStatusLedGpioFd = -1;
static bool statusLedOn = false;

// Timer / polling
static int buttonPollTimerFd = -1;
static int azureTimerFd = -1;
//static int epollFd = -1; //duplicated

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 5 ;  ////5  or  120
static const int AzureIoTMinReconnectPeriodSeconds = 20;  ////60  or   600
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60; //// 10 * 60 or   10 * 600

static int azureIoTPollPeriodSeconds = -1;

// Button state variables
static GPIO_Value_Type sendMessageButtonState = GPIO_Value_High;
static GPIO_Value_Type sendOrientationButtonState = GPIO_Value_High;

static void ButtonPollTimerEventHandler(EventData* eventData);
static bool IsButtonPressed(int fd, GPIO_Value_Type* oldState);
static void SendMessageButtonHandler(void);
static void SendOrientationButtonHandler(void);
static bool deviceIsUp = false; // Orientation
static void AzureTimerEventHandler(EventData* eventData);

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	terminationRequired = true;
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char* argv[])
{
	Log_Debug("IoT Hub/Central Application starting.\n");

	if (argc == 2) {
		Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
	}
	else {
		Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
		return -1;
	}

	if (InitPeripheralsAndHandlers() != 0) {
		terminationRequired = true;
	}

	// Main loop
	while (!terminationRequired) {
		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}
	}

	ClosePeripheralsAndHandlers();

	Log_Debug("Application exiting.\n");

	return 0;
}

/// <summary>
/// Button timer event:  Check the status of buttons A and B
/// </summary>
static void ButtonPollTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(buttonPollTimerFd) != 0) {
		terminationRequired = true;
		return;
	}
	SendMessageButtonHandler();
	SendOrientationButtonHandler();
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(azureTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	bool isNetworkReady = false;
	if (Networking_IsNetworkingReady(&isNetworkReady) != -1) {
		if (isNetworkReady && !iothubAuthenticated) {
			SetupAzureClient();
		}
	}
	else {
		Log_Debug("Failed to get Network state\n");
	}

	if (iothubAuthenticated) {
		SendSimulatedTemperature();
		IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
	}
}

// event handler data structures. Only the event handler field needs to be populated.
static EventData buttonPollEventData = { .eventHandler = &ButtonPollTimerEventHandler };
static EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	//	if (initI2c() == -1) {
	//		return -1;
	//	}

		// Create a UART_Config object, open the UART and set up UART event handler
	UART_Config uartConfig;
	UART_InitConfig(&uartConfig);
	uartConfig.baudRate = 9600;
	uartConfig.flowControl = UART_FlowControl_None;
	uartFd = UART_Open(SAMPLE_UART, &uartConfig);
	if (uartFd < 0) {
		Log_Debug("ERROR: Could not open UART: %s (%d).\n", strerror(errno), errno);
		return -1;
	}
	if (RegisterEventHandlerToEpoll(epollFd, uartFd, &uartEventData, EPOLLIN) != 0) {
		return -1;
	}
	// Open button A GPIO as input
	Log_Debug("Opening SAMPLE_BUTTON_1 as input\n");
	sendMessageButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_1);
	if (sendMessageButtonGpioFd < 0) {
		Log_Debug("ERROR: Could not open button A: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// Open button B GPIO as input
	Log_Debug("Opening SAMPLE_BUTTON_2 as input\n");
	sendOrientationButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_2);
	if (sendOrientationButtonGpioFd < 0) {
		Log_Debug("ERROR: Could not open button B: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// LED 4 Blue is used to show Device Twin settings state
	Log_Debug("Opening SAMPLE_LED as output\n");
	deviceTwinStatusLedGpioFd =
		GPIO_OpenAsOutput(SAMPLE_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	if (deviceTwinStatusLedGpioFd < 0) {
		Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// Set up a timer to poll for button events.
	struct timespec buttonPressCheckPeriod = { 0, 1000 * 1000 };
	buttonPollTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &buttonPressCheckPeriod, &buttonPollEventData, EPOLLIN);
	if (buttonPollTimerFd < 0) {
		return -1;
	}

	azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
	struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
	azureTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);
	if (buttonPollTimerFd < 0) {
		return -1;
	}

	return 0;
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
	Log_Debug("Closing file descriptors\n");

	// Leave the LEDs off
	if (deviceTwinStatusLedGpioFd >= 0) {
		GPIO_SetValue(deviceTwinStatusLedGpioFd, GPIO_Value_High);
	}

	CloseFdAndPrintError(buttonPollTimerFd, "ButtonTimer");
	CloseFdAndPrintError(azureTimerFd, "AzureTimer");
	CloseFdAndPrintError(sendMessageButtonGpioFd, "SendMessageButton");
	CloseFdAndPrintError(sendOrientationButtonGpioFd, "SendOrientationButton");
	CloseFdAndPrintError(deviceTwinStatusLedGpioFd, "StatusLed");
	CloseFdAndPrintError(uartFd, "Uart");
	CloseFdAndPrintError(epollFd, "Epoll");
}

/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
static void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
	IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
	void* userContextCallback)
{
	iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
	Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
	if (iothubClientHandle != NULL)
		IoTHubDeviceClient_LL_Destroy(iothubClientHandle);

	AZURE_SPHERE_PROV_RETURN_VALUE provResult =
		IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
			&iothubClientHandle);
	Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
		getAzureSphereProvisioningResultString(provResult));

	if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {

		// If we fail to connect, reduce the polling frequency, starting at
		// AzureIoTMinReconnectPeriodSeconds and with a backoff up to
		// AzureIoTMaxReconnectPeriodSeconds
		if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
			azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
		}
		else {
			azureIoTPollPeriodSeconds *= 2;
			if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
				azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
			}
		}

		struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
		SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

		Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
			azureIoTPollPeriodSeconds);
		return;
	}

	// Successfully connected, so make sure the polling frequency is back to the default
	azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
	struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
	SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

	iothubAuthenticated = true;

	if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
		&keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
		Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
		return;
	}

	IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, TwinCallback, NULL);
	IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle,
		HubConnectionStatusCallback, NULL);
}

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
///     Updates local state for 'showEvents' (bool).
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
	size_t payloadSize, void* userContextCallback)
{
	size_t nullTerminatedJsonSize = payloadSize + 1;
	char* nullTerminatedJsonString = (char*)malloc(nullTerminatedJsonSize);
	if (nullTerminatedJsonString == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
		abort();
	}

	// Copy the provided buffer to a null terminated buffer.
	memcpy(nullTerminatedJsonString, payload, payloadSize);
	// Add the null terminator at the end.
	nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

	JSON_Value* rootProperties = NULL;
	rootProperties = json_parse_string(nullTerminatedJsonString);
	if (rootProperties == NULL) {
		Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
		goto cleanup;
	}

	JSON_Object* rootObject = json_value_get_object(rootProperties);
	JSON_Object* desiredProperties = json_object_dotget_object(rootObject, "desired");
	if (desiredProperties == NULL) {
		desiredProperties = rootObject;
	}

	// Handle the Device Twin Desired Properties here.
	JSON_Object* LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
	if (LEDState != NULL) {
		statusLedOn = (bool)json_object_get_boolean(LEDState, "value");
		GPIO_SetValue(deviceTwinStatusLedGpioFd,
			(statusLedOn == true ? GPIO_Value_Low : GPIO_Value_High));
		TwinReportBoolState("StatusLED", statusLedOn);
	}

cleanup:
	// Release the allocated memory.
	json_value_free(rootProperties);
	free(nullTerminatedJsonString);
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
	static char* reasonString = "unknown reason";
	switch (reason) {
	case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
		reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
		break;
	case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
		break;
	case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
		reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
		break;
	case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
		break;
	case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
		break;
	case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
		reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
		break;
	case IOTHUB_CLIENT_CONNECTION_OK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
		break;
	}
	return reasonString;
}

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char* getAzureSphereProvisioningResultString(
	AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
	switch (provisioningResult.result) {
	case AZURE_SPHERE_PROV_RESULT_OK:
		return "AZURE_SPHERE_PROV_RESULT_OK";
	case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
		return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
	case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
	case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
	default:
		return "UNKNOWN_RETURN_VALUE";
	}
}

/// <summary>
///     Sends telemetry to IoT Hub
/// </summary>
/// <param name="key">The telemetry item to update</param>
/// <param name="value">new telemetry value</param>
static void SendTelemetry(const unsigned char* key, const unsigned char* value)
{
	static char eventBuffer[100] = { 0 };
	static const char* EventMsgTemplate = "{ \"%s\": \"%s\" }";
	int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, key, value);
	if (len < 0)
		return;

	Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

	IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);

	if (messageHandle == 0) {
		Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
		return;
	}

	if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
		/*&callback_param*/ 0) != IOTHUB_CLIENT_OK) {
		Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
	}
	else {
		Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
	}

	IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Sends telemetry to IoT Hub
/// </summary>
/// <param name="key">The telemetry item to update</param>
/// <param name="value">new telemetry value</param>
static void SendGPSPosition(const unsigned char* key, const unsigned char* value)
{
	static char eventBuffer[100] = { 0 };
	static const char* EventMsgTemplate = "{ \"%s\": %s }";
	int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, key, value);
	if (len < 0)
		return;

	Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

	IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);

	if (messageHandle == 0) {
		Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
		return;
	}

	if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
		/*&callback_param*/ 0) != IOTHUB_CLIENT_OK) {
		Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
	}
	else {
		Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
	}

	IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Callback confirming message delivered to IoT Hub.
/// </summary>
/// <param name="result">Message delivery status</param>
/// <param name="context">User specified context</param>
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context)
{
	Log_Debug("INFO: Message received by IoT Hub. Result is: %d\n", result);
}

/// <summary>
///     Creates and enqueues a report containing the name and value pair of a Device Twin reported
///     property. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
/// </summary>
/// <param name="propertyName">the IoT Hub Device Twin property name</param>
/// <param name="propertyValue">the IoT Hub Device Twin property value</param>
static void TwinReportBoolState(const char* propertyName, bool propertyValue)
{
	if (iothubClientHandle == NULL) {
		Log_Debug("ERROR: client not initialized\n");
	}
	else {
		static char reportedPropertiesString[30] = { 0 };
		int len = snprintf(reportedPropertiesString, 30, "{\"%s\":%s}", propertyName,
			(propertyValue == true ? "true" : "false"));
		if (len < 0)
			return;

		if (IoTHubDeviceClient_LL_SendReportedState(
			iothubClientHandle, (unsigned char*)reportedPropertiesString,
			strlen(reportedPropertiesString), ReportStatusCallback, 0) != IOTHUB_CLIENT_OK) {
			Log_Debug("ERROR: failed to set reported state for '%s'.\n", propertyName);
		}
		else {
			Log_Debug("INFO: Reported state for '%s' to value '%s'.\n", propertyName,
				(propertyValue == true ? "true" : "false"));
		}
	}
}

/// <summary>
///     Callback invoked when the Device Twin reported properties are accepted by IoT Hub.
/// </summary>
static void ReportStatusCallback(int result, void* context)
{
	Log_Debug("INFO: Device Twin reported properties update result: HTTP status code %d\n", result);
}

/// <summary>
///     Generates a simulated Temperature and sends to IoT Hub.
/// </summary>
void SendSimulatedTemperature(void)
{
	char gpsposition[50];
	snprintf(gpsposition, sizeof(gpsposition), "{\"lon\": %f, \"lat\": %f}", minmea_tocoord(&frame.longitude), minmea_tocoord(&frame.latitude));
	static float temperature = 30.0;
	float deltaTemp = (float)(rand() % 20) / 20.0f;
	if (rand() % 2 == 0) {
		temperature += deltaTemp;
	}
	else {
		temperature -= deltaTemp;
	}

	char tempBuffer[20];
	int len = snprintf(tempBuffer, 20, "%3.2f", temperature);
	if (len > 0)
	{
		//	SendTelemetry("GPSRawData", (char*)danewyjsciowe);
		SendGPSPosition("assetloc", (char*)gpsposition);
	}
}

/// <summary>
///     Check whether a given button has just been pressed.
/// </summary>
/// <param name="fd">The button file descriptor</param>
/// <param name="oldState">Old state of the button (pressed or released)</param>
/// <returns>true if pressed, false otherwise</returns>
static bool IsButtonPressed(int fd, GPIO_Value_Type* oldState)
{
	bool isButtonPressed = false;
	GPIO_Value_Type newState;
	int result = GPIO_GetValue(fd, &newState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
	}
	else {
		// Button is pressed if it is low and different than last known state.
		isButtonPressed = (newState != *oldState) && (newState == GPIO_Value_Low);
		*oldState = newState;
	}

	return isButtonPressed;
}

/// <summary>
/// Pressing button A will:
///     Send a 'Button Pressed' event to Azure IoT Central
/// </summary>
static void SendMessageButtonHandler(void)
{
	if (IsButtonPressed(sendMessageButtonGpioFd, &sendMessageButtonState)) {
		SendTelemetry("ButtonPress", "True");
	}
}

/// <summary>
/// Pressing button B will:
///     Send an 'Orientation' event to Azure IoT Central
/// </summary>
static void SendOrientationButtonHandler(void)
{
	if (IsButtonPressed(sendOrientationButtonGpioFd, &sendOrientationButtonState)) {
		deviceIsUp = !deviceIsUp;
		SendTelemetry("Orientation", deviceIsUp ? "Up" : "Down");
	}
}

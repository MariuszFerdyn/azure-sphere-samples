# Sample: AzureIoT+GPS

This sample demonstrates how to get data from GPS and put it to IoT Central and display on Map.
This sample is modyfication of AzureIoT sample.
Hardware used Azure Sphere AVNET + GPS NEO-6M
[How is it working](https://youtu.be/I-6VPgB3sU4?t=1).


**IMPORTANT**: This sample application requires customization before it will compile and run. Follow the instructions in this README and in IoTCentral.md and/or IoTHub.md to perform the necessary steps.

This application does the following:

- Sends GPS position to Azure IoT Central or an Azure IoT Hub at regular intervals.
- In IoT Central current location can be display on map.

Before you can run the sample, you must configure either an Azure IoT Central application or an Azure IoT Hub, and modify the sample's application manifest to enable it to connect to the Azure IoT resources that you configured.

By default, this sample runs over a Wi-Fi connection to the internet. To use Ethernet instead, make the following changes:

1. Configure Azure Sphere as described in [Connect Azure Sphere to Ethernet](https://docs.microsoft.com/azure-sphere/network/connect-ethernet).
1. Add an Ethernet adapter to your hardware. If you are using an MT3620 RDB, see the [wiring instructions](../../Hardware/mt3620_rdb/EthernetWiring.md).
1. Add the following line to the Capabilities section of the app_manifest.json file:
   `"NetworkConfig" : "true"`
1. In main.c, add the following lines before any other networking calls:

    ```c
     err = Networking_SetInterfaceState("eth0", true);
     if (err < 0) {
           Log_Debug("Error setting interface state %d\n",errno);
           return -1;
       }
    ```                                                 

The sample uses these Azure Sphere application libraries.

|Library   |Purpose  |
|---------|---------|
|log     |  Displays messages in the Visual Studio Device Output window during debugging  |
| networking | Determines whether the device is connected to the internet |
| gpio | Manages buttons A and B and LED 4 on the device |
|storage    | Gets the path to the certificate file that is used to authenticate the server      |
|uart    | Gets the path to the certificate file that is used to authenticate the server      |

## Prerequisites

The sample requires the following software:

- Azure Sphere SDK version 19.05 or later. In an Azure Sphere Developer Command Prompt, run **azsphere show-version** to check. Download and install the [latest SDK](https://aka.ms/AzureSphereSDKDownload) if necessary.
- An Azure subscription. If your organization does not already have one, you can set up a [free trial subscription](https://azure.microsoft.com/free/?v=17.15).

## Preparation

**Note:** By default, this sample targets [MT3620 reference development board (RDB)](https://docs.microsoft.com/azure-sphere/hardware/mt3620-reference-board-design) hardware, such as the MT3620 development kit from Seeed Studios. To build the sample for different Azure Sphere hardware, change the Target Hardware Definition Directory in the project properties. For detailed instructions, see the [README file in the Hardware folder](../../Hardware/README.md). 

1. Set up your Azure Sphere device and development environment as described in the [Azure Sphere documentation](https://docs.microsoft.com/azure-sphere/install/overview).
1. Clone the Azure Sphere Samples repository on GitHub and navigate to the AzureIoT folder.
1. Connect your Azure Sphere device to your PC by USB.
1. Enable a network interface on your Azure Sphere device and verify that it is connected to the internet.
1. Open an Azure Sphere Developer Command Prompt and enable application development on your device if you have not already done so:

   `azsphere device prep-debug`

## Run the sample

- [Run the sample with Azure IoT Central](./IoTCentral.md)
- To display MAP please add it to IoTCentral like here: [IoT Central GPS Location](https://sandervandevelde.wordpress.com/2019/07/07/tracking-the-location-of-your-iot-central-devices/)

## License

For details on license, see LICENSE.txt in each directory
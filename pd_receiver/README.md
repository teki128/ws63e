# WS63E HH-D02 PD receiver port

This directory contains the WS63E implementation. Copy it to
`C:/Env/Hi3863/SDK/fbb_ws63/src/application/samples/custom/pd_receiver`, copy
the unchanged `data_frame.h`, `spooky_decoder.c/.h`, and
`spooky_encoder.c/.h` into the same directory, and create the parent
`custom/CMakeLists.txt` containing `add_subdirectory_if_exist(pd_receiver)`.

Before building, copy `wifi_credentials.example.h` to `wifi_credentials.h`,
then edit `WIFI_SSID`, `WIFI_PASS`, and `PC_IP`. The credentials file is
excluded from Git.

HH-D02 board manual V1.2 confirms that IO7 is exposed on the upper 2.54 mm
header. Connect the PD analog output to that IO7 pin and connect grounds. The
on-board USER button uses GPIO13, so it does not conflict with ADC0/GPIO7.
The same manual identifies the Type-C connector as the power/programming/UART
port; serial output is 115200 baud and the flashing tool selects the WS63 chip
family for WS63E.

`pd_adc_stream.c` is a public-SDK validation backend. The bundled V154 HAL
retains only two samples and reports them on scan disable, so it cannot
guarantee the requested continuous 64 kS/s stream. When the vendor supplies a
continuous FIFO/DMA implementation, replace only `pd_adc_stream.c`; the
receiver, decoder, queue, Wi-Fi, and UDP architecture remains unchanged.

#include "usbpp.h"
using namespace std::literals;

int main(){
	usbpp::context ctx = usbpp::init();
	usbpp::device_handle printer = usbpp::open_device_with_vid_pid(ctx, 0x154f, 0x154f);
	usbpp::reset_device(printer);
	usbpp::set_configuration(printer);
	usbpp::Interface interface = usbpp::claim_interface(printer, 0);
	usbpp::bulk_transfer(printer, 2, "hello, world\n\n\n\n\n\n\n\n\n\n"s);
}

#include <libusb-1.0/libusb.h>
#include <memory>
#include <ranges>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <exception>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "tinyformat.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "usbpp.h"

using std::clog;
using std::cerr;
using std::vector;
using std::endl;
using std::ranges::find_if;

int main(){
	usbpp::context ctx = usbpp::init();

	vector<usbpp::device> devices = usbpp::get_device_list(ctx);
	clog << "# devices: " << devices.size() << endl;

	auto printers = find_if(devices, [](const auto& dev){
		auto d = usbpp::get_device_descriptor(dev);
		return d.idVendor == 0x154f && d.idProduct == 0x154f;
	});

	if(printers == devices.end()){
		clog << "No printer fonud\n";
		return 1;
	}

	usbpp::device_handle printer = usbpp::open(*printers);

	libusb_reset_device(printer.get());
	usbpp::set_configuration(printer);

	
	usbpp::Interface interface = usbpp::claim_interface(printer, 0);
	
	for(int i=0; i < 100; i++){
	std::string hw = "<3<3<3<3<3<3<3<3<3<3<3<3\n";
	vector<char> h{hw.begin(), hw.end()};

	usbpp::write(printer, 2, hw);
	usbpp::write(printer, 2, "hello");

	libusb_bulk_transfer(printer.get(), 0x02, (unsigned char*)hw.data(), hw.size(), nullptr, 0);
	}
	cerr << "lol\n";
}

#include "usbpp.h"
#include "tinyformat.h"

#include <iomanip>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "tinyformat.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif



using std::clog;
using std::cerr;
using std::vector;
using std::endl;
using std::ranges::find_if;

using namespace std::literals;

template<usbpp::ByteData R>
std::vector<std::ranges::range_value_t<R>> copy(const R& range){
	using std::begin;
	using std::end;
	return {begin(range), end(range)};
}

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

	usbpp::reset_device(printer);
	usbpp::set_configuration(printer);

	
	usbpp::Interface interface = usbpp::claim_interface(printer, 0);
	
	usbpp::bulk_transfer(printer, 2, "<3<3<3<3<3<3<3<3<3"s);
	usbpp::bulk_transfer(printer, 2, copy("hello\n"));

	cerr << "lol\n";
}

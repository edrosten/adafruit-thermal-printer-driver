#include <libusb-1.0/libusb.h>
#include <memory>
#include <vector>
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

namespace usbpp{
	// Simple C++ wrapper arund libUSB
	// Documentation is mostly in libUSB, with matching names for 
	// functions and types
	//    libusb_function_name -> usbpp::function_name
	//    libusb_type_name -> usbpp::type_name
	//
	// Design goal is to make clearer code by avoiding mixing 
	// book keeping with the main logic. This means:
	//    Automatic resource management / clear ownrship
	//    Automatic error checking
	//
	// Not all libUSB types and functions are wrapped.
	// Wrapping provides only the following:
	//    RAII for types which need destruction
	//    exceptions instead of error codes
	//    return values instead of return arguments 

	template<typename T, void(*del)(T*)>
	using Handle = std::unique_ptr<T, decltype([](T*x){del(x);})>;

	class Error: public std::runtime_error{
		public:
			Error(int err_code)
			:runtime_error(libusb_error_name(err_code)),_code(err_code)
			{
			}
			int code() const{
				return _code;
			}
		private:
			int _code;
	};

	void check(int err){
		if(err < 0){
			throw Error(err);
		}
	}

	using context = Handle<libusb_context, libusb_exit>;
	inline context init(){
		libusb_context* ctx = nullptr;
		check(libusb_init(&ctx));
		return context{ctx};
	}

	using device = Handle<libusb_device, libusb_unref_device>;
	inline std::vector<device> get_device_list(context& ctx){

		libusb_device **list = nullptr;
		int n = libusb_get_device_list(ctx.get(), &list);
		check(n);

		std::vector<device> ret;
		for(int i=0; i < n; i++)
			ret.emplace_back(list[i]);

		libusb_free_device_list(list, false);
		return ret;
	}
	
	using device_descriptor = libusb_device_descriptor;
	device_descriptor get_device_descriptor(const device& dev){
		device_descriptor ret;
		check(libusb_get_device_descriptor(dev.get(), &ret));
		return ret;
	}

	using device_handle = Handle<libusb_device_handle, libusb_close>;
	inline device_handle open(device& dev){
		libusb_device_handle *hnd = nullptr;
		int err = libusb_open(dev.get(), &hnd);
		check(err);
		return device_handle{hnd};
	}

	inline int get_configuration(device_handle& handle){
		int config=0;
		check(libusb_set_configuration(handle.get(), config));
		return config;
	}
	
	// Defaults to 1, the most common value by a very long way
	// according to pyusb!
	inline void set_configuration(device_handle& handle, int config=1){
		check(libusb_set_configuration(handle.get(), config));
	}
		
	class Interface{
		static constexpr int Invalid = -1;
		int handle=Invalid;
		libusb_device_handle* dev = nullptr;

		public:
			explicit Interface(int i, device_handle& dev) noexcept
			:handle(i),dev(dev.get())
			{}

			Interface(const Interface&)=delete;
			Interface& operator=(const Interface&)=delete;

			Interface(Interface&& from){
				(*this) = std::move(from);
			}

			Interface& operator=(Interface&& from){
				release_interface();
				handle = from.handle;
				dev = from.dev;
				from.handle = Invalid;
				return *this;
			}

			void release_interface(){
				if(handle != Invalid){
					int h = handle;
					handle = Invalid;
					std::cerr << 1;
					check(libusb_release_interface(dev, h));
					std::cerr << 2;
				}
			}

			~Interface(){
				try{
					release_interface();
				}
				catch(const Error& e){
					std::cerr << "Failed to release interface: " << e.what() << std::endl;
				}
			}
	};

	Interface claim_interface(device_handle& dev, int interface){
		usbpp::check(libusb_claim_interface(dev.get(), interface));
		return Interface{interface, dev};
	}
};

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

	libusb_bulk_transfer(printer.get(), 0x02, (unsigned char*)hw.data(), hw.size(), nullptr, 0);
	}
	cerr << "lol\n";
}

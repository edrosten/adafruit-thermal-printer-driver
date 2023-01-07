#ifndef INC_USBPP_H_0a2b4fc07fb666340830a2a9d56a73aa
#define INC_USBPP_H_0a2b4fc07fb666340830a2a9d56a73aa
#include <libusb-1.0/libusb.h>
#include <memory>
#include <ranges>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <exception>

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
	//    reasonable use of sensible types for buffers

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

	template<typename T, void(*del)(T*)>
	using Handle = std::unique_ptr<T, decltype([](T*x){del(x);})>;


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
		
		// Create device handles for all the devices
		std::vector<device> ret;
		for(int i=0; i < n; i++)
			ret.emplace_back(list[i]);
		
		// Free the outer list, but not the handles
		libusb_free_device_list(list, false);
		return ret;
	}
	
	using device_descriptor = libusb_device_descriptor;
	inline device_descriptor get_device_descriptor(const device& dev){
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

	inline device_handle open_device_with_vid_pid(context& ctx, uint16_t vid, uint16_t pid){
		device_handle ret{libusb_open_device_with_vid_pid(ctx.get(), vid, pid)};
		if(ret == nullptr)
			throw Error(LIBUSB_ERROR_NOT_FOUND);
		return ret;
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

		int try_release(){
			if(handle != Invalid){
				int h = handle;
				handle = Invalid;
				return libusb_release_interface(dev, h);
			}
		}

		public:
			explicit Interface(int i, device_handle& dev) noexcept
			:handle(i),dev(dev.get())
			{}

			Interface(const Interface&)=delete;
			Interface& operator=(const Interface&)=delete;

			Interface(Interface&& from){
				(*this) = std::move(from);
			}

			void release_interface(){
				check(try_release());
			}

			Interface& operator=(Interface&& from){
				release_interface();
				handle = from.handle;
				dev = from.dev;
				from.handle = Invalid;
				return *this;
			}

			~Interface(){
				if(int e; (e=libusb_release_interface(dev, handle)) != 0)
					std::cerr << "Failed to release interface: " << Error(e).what() << "\n";
			}
	};


	Interface claim_interface(device_handle& dev, int interface){
		usbpp::check(libusb_claim_interface(dev.get(), interface));
		return Interface{interface, dev};
	}

	template<typename T, typename... Args>
	constexpr bool one_of = (... || std::same_as<T, Args>);


	template<typename T> concept NonConstByteData = 
		std::ranges::contiguous_range<T> && 
		one_of<std::ranges::range_value_t<T>, char, unsigned char, std::byte> &&
		!std::is_const_v<T>;

	template<NonConstByteData Range>
	int bulk_transfer(device_handle& dev, int endpoint, Range&& range, std::chrono::milliseconds timeout=std::chrono::milliseconds{0}){
		using std::begin;
		using std::end;
		int sent=0;
		int err=libusb_bulk_transfer(dev.get(), endpoint, reinterpret_cast<unsigned char*>(&*begin(range)), end(range)-begin(range), &sent, timeout.count());
		check(err);
		return sent;
	}

	void reset_device(device_handle& dev){
		check(libusb_reset_device(dev.get()));
	}

	using config_descriptor = Handle<libusb_config_descriptor, libusb_free_config_descriptor>;
	inline config_descriptor get_config_descriptor(device& dev, uint8_t config_index){
		libusb_config_descriptor *desc = nullptr;
		check(libusb_get_config_descriptor(dev.get(), config_index, &desc));
		return config_descriptor{desc};
	}

	using config_descriptor = Handle<libusb_config_descriptor, libusb_free_config_descriptor>;
	inline config_descriptor get_config_descriptor_by_value(device& dev, uint8_t bConfigurationValue){
		libusb_config_descriptor *desc = nullptr;
		check(libusb_get_config_descriptor_by_value(dev.get(), bConfigurationValue, &desc));
		return config_descriptor{desc};
	}
};

#endif

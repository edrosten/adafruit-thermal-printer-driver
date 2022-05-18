#include <cups/sidechannel.h>
#include <cups/raster.h>

#include <iostream>
#include <fstream>
#include <streambuf>
#include <ostream>
#include <vector>
#include <array>
#include <utility>
#include <cmath>
#include <algorithm>
#include <thread>

#include <signal.h>
#include <unistd.h>
#include <sys/fcntl.h>

using std::cerr;
using std::cerr;
using std::cout;
using std::flush;
using std::endl;
using std::min;
using std::max;
using std::vector;
using std::array;
using std::string;

constexpr unsigned char ESC = 0x1b;
constexpr unsigned char GS = 0x1d;
constexpr unsigned char DC2 = 0x12;

// Write out a std::array of bytes as bytes. 
template<size_t N>
std::ostream& operator<<(std::ostream& out, const array<unsigned char, N>& a){
	out.write(reinterpret_cast<const char*>(a.data()), a.size());
	return out;
}

array<unsigned char, 2> binary(uint16_t n){
	return {{static_cast<unsigned char>(n&0xff), static_cast<unsigned char>(n >> 8)}};
}

void printerInitialise(){
	cout << ESC << '\x40';
	//If you dont flush here, then the initialise command can kill some of the
	//data in the buffer, leading to the printer to eat some control codes and
	//print out junk.
	cout << flush;
}

void transmit_status(){
	cout << GS << "r1" << flush;
}


// enter raster mode and set up x and y dimensions
void rasterheader(uint16_t xsize, uint16_t ysize)
{
	// Page 33 of the manual
	// The x size is the number of bytes per row, so the number of pixels
	// is always a multiple of 8
	cout << GS << 'v' << '0' << '\0' << binary((xsize+7)/8) << binary(ysize);
}

void feed_lines(int n){
	for(;n >0 ; n-=255)
		cout << ESC << 'J' << static_cast<unsigned char>(min(255, n));
}

void feed_mm(int mm){
	feed_lines(mm*8);
}

void horizontal_rule(int w=384){
	rasterheader(w, 1);
	cout << string((w+7)/8, '\xff');
}

void set_density(int print_density_percent, int print_break_time_us) {
	// Page 48 of the manual
	cout << DC2 << '#' << (char)((print_break_time_us / 250 << 5) | ((print_density_percent - 50) / 5 & 0b11111));
}

void set_heating_time(int time_factor=80){
	// Page 47 of the manual
	// All the defaults are used if not overridden
	cout << ESC << '7' << (char)7 << (unsigned char)std::max(3, std::min(255,time_factor)) << '\02';
}

constexpr array<array<int, 5>, 3> diffusion_coefficients = {{
		{{0, 0, 0, 7, 5}},
		{{3, 5, 7, 5, 3}},
		{{1, 3, 5, 3, 1}}
}};
constexpr double diffusion_divisor=48;


double degamma(int p){
	return pow(p/255., 1/2.2);
}


void wait_for_lines(const int lines_sent, int& read_back, int max_diff){
	//We've stuffed requests into the command stream which reply with bytes
	//for each line printed. This waits for replies until the number of 
	//relplies is within some threshold of the number of requests sent.
	//
	//This is for buffer management.
	//
	//Also, if there's no change in the number of lines read for over some
	//time threshold then the printer has stopped which means out of paper
	//since it has only one sensor.
	using namespace std::literals;
	using namespace std::chrono;

	auto time_of_last_change = steady_clock::now();
	bool has_paper=true;

	for(;;){
		char buf;
		ssize_t bytes_read = cupsBackChannelRead(&buf, 1, 0.0);

		if(bytes_read > 0){
			read_back++;
			
			if(!has_paper){
				cerr << "STATE: -media-empty\n";
				cerr << "STATE: -media-needed\n";
				cerr << "STATE: -cover-open\n";
				cerr << "INFO: Printing\n";
			}
				
			has_paper = true;
			time_of_last_change = steady_clock::now();
		}
		else if(auto interval = steady_clock::now() - time_of_last_change; interval > 2500ms){
			cerr << "DEBUG: no change for " << duration_cast<seconds>(interval).count() << " seconds, assuming no paper\n";
			if(has_paper){
				cerr << "STATE: +media-empty\n";
				cerr << "STATE: +media-needed\n";
				cerr << "STATE: +cover-open\n";
				cerr << "INFO: Printer door open or no paper left\n";
			}
			has_paper = false;
		}

		cerr << "DEBUG: Lines sent=" << lines_sent << " lines printed=" << read_back << "\n";

		if(lines_sent - read_back <= max_diff)
			break;

		cerr << "DEBUG: buffer too full (" << lines_sent - read_back << "), pausing...\n";
		std::this_thread::sleep_for(100ms);
	}
}
volatile sig_atomic_t cancel_job = 0;

std::ofstream *wtf;

void set_heating_time_basic(int heating_dots, int heating_time_us, int heating_interval_us)
{
	//Reject illegal values
	if(heating_dots < 8)
		return;
	if(heating_time_us < 30)
		return;
	
	int dots = min(255,max(0,heating_dots/8 - 1));
	int time = min(255, max(0,heating_time_us / 10));
	int interval = min(255,max(0,heating_interval_us / 10));

	// Page 47 of the manual
	// All the defaults are used if not overridden
	cout << ESC << '7' << (unsigned char)dots << (unsigned char)time << (unsigned char)interval;
}

int main(int argc, char** argv){
	
	cerr << "STATE: -media-empty\n";
	cerr << "STATE: -media-needed\n";
	cerr << "STATE: -cover-open\n";
	cerr << "INFO: Printing\n";
	
	cerr << "DEBUG: Starting the AdaFruit Mini Driver, by E. Rosten\n";
	if(argc !=6 && argc != 7){
		cerr << "ERROR: " << argv[0] << " job-id user title copies options [file]\n";
		exit(1);
	}

	int input_fd = 0;
	if(argc == 7){
		input_fd = open(argv[6], O_RDONLY);
		if(input_fd == -1){
			cerr << "ERROR: could not open file " << argv[6] << ": " << strerror(errno) << endl;
			exit(1);
		}
	}
	
	//As recommended by the CUPS documentation https://www.cups.org/doc/api-filter.html#SIGNALS
	signal(SIGPIPE, SIG_IGN);

	{
		struct sigaction int_action;
		memset(&int_action, 0, sizeof(int_action));
		sigemptyset(&int_action.sa_mask);
		int_action.sa_handler = [](int){
			cancel_job=1;
		};
		sigaction(SIGTERM, &int_action, nullptr);
	}

	cups_raster_t *ras = cupsRasterOpen(input_fd, CUPS_RASTER_READ);
	cups_page_header2_t header;
	int page = 0;


	int feed_between_pages_mm = 0;
	int mark_page_boundary = 0;
	int eject_after_print_mm = 0;
	int auto_crop = 0;
	int enhance_resolution = 0;

	int heating_dots = 64;
	int heating_time_us = 800;
	int heating_interval_us = 20;

	int print_density_percent = 100;
	int print_break_time_us = 500;

	printerInitialise();
	//This isn't cleared by initialize, so reset it to the 
	//default just in case.
	set_heating_time();

	int lines_sent = 0;
	int read_back= 0;

	while (cupsRasterReadHeader2(ras, &header))
	{
		//Read the configuration parameters
		feed_between_pages_mm = header.cupsInteger[0];
		mark_page_boundary = header.cupsInteger[1];
		eject_after_print_mm = header.cupsInteger[2];
		auto_crop = header.cupsInteger[3];
		enhance_resolution = header.cupsInteger[4];
		heating_dots = header.cupsInteger[5];
		heating_time_us = header.cupsInteger[6];
		heating_interval_us = header.cupsInteger[7];
		print_density_percent = header.cupsInteger[8];
		print_break_time_us = header.cupsInteger[9];

		page++;
		//Write out information to CUPS 
		cerr << "PAGE: " << page << " " << header.NumCopies << "\n";
		cerr << "DEBUG: bitsperpixel " << header.cupsBitsPerPixel << endl;
		cerr << "DEBUG: BitsPerColor " << header.cupsBitsPerColor << endl;
		cerr << "DEBUG: Width " << header.cupsWidth << endl;
		cerr << "DEBUG: Height" << header.cupsHeight << endl;
		cerr << "DEBUG: feed_between_pages_mm " << feed_between_pages_mm << endl;
		cerr << "DEBUG: mark_page_boundary " << mark_page_boundary << endl;
		cerr << "DEBUG: eject_after_print_mm " << eject_after_print_mm << endl;
		cerr << "DEBUG: auto_crop " << auto_crop << endl;
		cerr << "DEBUG: enhance_resolution " << enhance_resolution << endl;
		cerr << "DEBUG: heating_dots " << heating_dots << endl;
		cerr << "DEBUG: heating_time_us " << heating_time_us << endl;
		cerr << "DEBUG: heating_interval_us " << heating_interval_us << endl;
		cerr << "DEBUG: print_density_percent " << print_density_percent << endl;
		cerr << "DEBUG: print_break_time_us " << print_break_time_us << endl;

		//Set print density
		set_density(print_density_percent, print_break_time_us);

		if(!enhance_resolution)
			set_heating_time_basic(heating_dots,heating_time_us,heating_interval_us);

		if(page > 1){
			cerr << "DEBUG: page feeding " << eject_after_print_mm << "mm\n";
			feed_mm(feed_between_pages_mm);
		}

		//Avoid double marking the boundary if there's no feed
		if(mark_page_boundary && (feed_between_pages_mm > 0 || page == 1)){
			cerr << "DEBUG: emitting page rule at page top\n";

			//Reset the line to all dark for the rule
			if(enhance_resolution)
				set_heating_time();
			horizontal_rule();
		}

		// Input data buffer for one line
		vector<unsigned char> buffer(header.cupsBytesPerLine);
		
		//Error diffusion data
		vector<vector<double>> errors(diffusion_coefficients.size(), vector<double>(buffer.size(), 0.0));

		/* read raster data */
		unsigned int n_blank=0;
		for (unsigned int y = 0; y < header.cupsHeight; y ++)
		{
			if(cancel_job){
				//We have ensured by this point that the output is never a half
				//written raster line, so anything sent here will be interpreted as 
				//a command not simply more data.
				cerr << "DEBUG: job cancelled\n";
				
				//This causes anything in the buffer to actually be printed
				//The printer stores segeral lines of bitmap worth and then
				//prints them. If that buffer is partially full right at the end then
				//things can go missing print wise.
				feed_lines(1);
				cout << flush;
				
				//Reset the heater to the default so that the cancelled message appears
				set_heating_time();
				cout << flush<<endl;
				cout << "*** Cancelled ***\n";
				
				//Unconditionally eject the cancelled job for tearoff
				//10mm is sufficient to clear the case
				feed_mm(10);
				cout << flush;
				goto finish;
			}

			if(cupsRasterReadPixels(ras, buffer.data(), header.cupsBytesPerLine) == 0)
				break;
			
			//Count blank lines rather than printing. Required for auto-cropping
			if(find_if(buffer.begin(), buffer.end(), [](auto c){return c!=255;}) == buffer.end()){
				n_blank++;
				continue;
			}
			
			//Auto crop means automatically remove white from the beginning and end of the page
			if(!auto_crop || n_blank != y){
				if(n_blank){
					cerr << "DEBUG: Feeding " << n_blank << " lines\n";

					//Emit blank lines as feed commands, for faster printing. However this messes
					//up the temperature calibration, so don't do it in resolution enhanced mode.
					if(!enhance_resolution)
						feed_lines(n_blank);
					else{
						for(unsigned int i=0; i < n_blank; i++){
							rasterheader(header.cupsWidth, 1);
							for(unsigned int j=0; j < header.cupsBytesPerLine; j++)
								cout << '\x00';
						}
					}
				}
			}
			else
				cerr << "DEBUG: AutoCrop skipping start " << n_blank << " lines\n";

			n_blank = 0;

			//Estimate the lowest value pixel in the row
			double low_val=1.0;
			for(int i=0; i < (int)buffer.size(); i++)
				 low_val = std::min(low_val, degamma(buffer[i]) + errors[0][i]);
			//Add some headroom otherwise black areas bleed because it can't go
			//dark enough
			low_val*=0.99;

			if(!enhance_resolution)
				low_val = 0;

			//Set the darkness based on the darkest pixel we want

			//Emperical formula for the effect of the timing
			double full_white=16;
			double full_black=16*7;

			if(enhance_resolution){
				set_heating_time(pow(1-low_val,2.0)*(full_black-full_white)+full_white);
			}

			//Print in MSB format, one line at a time
			rasterheader(header.cupsWidth, 1);

			unsigned char current=0;
			int bits=0;

			for(int i=0; i < (int)buffer.size(); i++){
				
				//The actual pixel value with gamma correction
				double pixel = degamma(buffer[i]) + errors[0][i];
				double actual = pixel>(1-low_val)/2 + low_val?1:low_val;
				double error = pixel - actual; //This error is then distributed
				
				//Diffuse forward the error	
				for(int r=0; r < (int)diffusion_coefficients.size(); r++)
					for(int cc=0; cc < (int)diffusion_coefficients[0].size(); cc++){
						int c = cc - diffusion_coefficients[0].size()/2;
						if(c+i >= 0 && c+i < (int)buffer.size() && diffusion_coefficients[r][cc]){
							errors[r][i+c] += error * diffusion_coefficients[r][cc] / diffusion_divisor;
						}
					}

				current |= (actual!=1)<<(7-bits);
				bits++;
				if(bits == 8){
					cout << current;
					bits = 0;
					current = 0;
				}
			}
			if(bits)
				cout << current;
		
			

			//Roll the buffer round.
			std::rotate(errors.begin(), errors.begin()+1, errors.end());
			for(auto& p:errors.back())
				p=0;
		

			//Stuff requests for paper status into the command stream
			//and count the returns. We allow a gap of 80 lines (1cm of printing)
			transmit_status();
			lines_sent++;
			wait_for_lines(lines_sent, read_back, 80);
		}

		//Finish page
		if(!auto_crop){
			cerr << "DEBUG: Feeding " << n_blank << " lines at end\n";
			feed_lines(n_blank);
		}
		else
			cerr << "DEBUG: AutoCrop skipping end " << n_blank << " lines\n";

		if(mark_page_boundary){
			cerr << "DEBUG: emitting page rule at page end\n";

			//Reset the line to all dark for the rule
			if(enhance_resolution)
				set_heating_time();
			horizontal_rule();
		}

	
	}

	feed_mm(eject_after_print_mm);


	finish:
	cerr << "DEBUG: end of print feeding " << eject_after_print_mm << "mm\n";
	cerr << "DEBUG: end status " << cancel_job << "\n";

	//Reset the printer to the default state. 
	set_heating_time();
	cout << flush;
	//using namespace std::literals;
	//std::this_thread::sleep_for(10s);
	cupsRasterClose(ras);
}

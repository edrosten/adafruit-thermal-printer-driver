#include <cups/raster.h>

#include <iostream>
#include <streambuf>
#include <ostream>
#include <vector>
#include <array>
#include <utility>
#include <cmath>
#include <algorithm>

#include <signal.h>
#include <unistd.h>
#include <sys/fcntl.h>

using std::clog;
using std::cerr;
using std::cout;
using std::endl;
using std::min;
using std::max;
using std::vector;
using std::array;
using std::string;

constexpr unsigned char ESC = 0x1b;
constexpr unsigned char GS = 0x1d;

// Write out a std::array of bytes as bytes.  This will form the basis
// of sending data to the printer.
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
	cout << string('\xff', (w+7)/8);
}

void set_heating_time(int time_factor){
	// Page 47 of the manual
	// Everything is default except the heat time
	cout << ESC << 7 << (char)7 << (unsigned char)std::max(3, std::min(255,time_factor)) << '\02';
}

constexpr array<array<int, 5>, 3> diffusion_coefficients = {{
		{{0, 0, 0, 7, 5}},
		{{3, 5, 7, 5, 3}},
		{{1, 3, 5, 3, 1}}
}};
constexpr double diffusion_divisor=42;


double degamma(int p){
	return pow(p/255., 1/2.2);
}


volatile sig_atomic_t cancel_job = 0;



int main(int argc, char** argv){

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
	signal(SIGTERM, [](int){ cancel_job = 1;});

	cups_raster_t *ras = cupsRasterOpen(input_fd, CUPS_RASTER_READ);
	cups_page_header2_t header;
	int page = 0;

	printerInitialise();

	int feed_between_pages_mm = 0;
	int mark_page_boundary = 0;
	int eject_after_print_mm = 0;
	int auto_crop = 0;
	int enhance_resolution = 0;

	while (cupsRasterReadHeader2(ras, &header))
	{
		page++;
		//Write out information to CUPS 
		clog << "PAGE: " << page << " " << header.NumCopies << "\n";
		clog << "DEBUG: bitsperpixel " << header.cupsBitsPerPixel << endl;
		clog << "DEBUG: BitsPerColor " << header.cupsBitsPerColor << endl;
		clog << "DEBUG: Width " << header.cupsWidth << endl;
		clog << "DEBUG: Height" << header.cupsHeight << endl;


		//Read the configuration parameters
		feed_between_pages_mm = header.cupsInteger[0];
		mark_page_boundary = header.cupsInteger[1];
		eject_after_print_mm = header.cupsInteger[2];
		auto_crop = header.cupsInteger[3];
		enhance_resolution = header.cupsInteger[4];

		if(page > 1){
			clog << "DEBUG: page feeding " << eject_after_print_mm << "mm\n";
			feed_mm(feed_between_pages_mm);
		}

		//Avoid double marking the boundary if there's no feed
		if(mark_page_boundary && (feed_between_pages_mm > 0 || page == 1)){
			clog << "DEBUG: emitting page rule at page top\n";
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
				goto finish;
			}

			if(cupsRasterReadPixels(ras, buffer.data(), header.cupsBytesPerLine) == 0)
				break;
			
			if(find_if(buffer.begin(), buffer.end(), [](auto c){return c!=255;}) == buffer.end()){
				n_blank++;
				continue;
			}
			
			//Auto crop means automatically remove white from the beginning and end of the page
			if(!auto_crop || n_blank != y){
				clog << "DEBUG: Feeding " << n_blank << "lines\n";
				feed_lines(n_blank);
			}
			else
				clog << "DEBUG: AutoCrop skipping start " << n_blank << "lines\n";

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
			set_heating_time(pow(1-low_val,2.0)*(full_black-full_white)+full_white);

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
		}

		//Finish page
		if(!auto_crop){
			clog << "DEBUG: Feeding " << n_blank << "lines at end\n";
			feed_lines(n_blank);
		}
		else
			clog << "DEBUG: AutoCrop skipping end " << n_blank << "lines\n";

		if(mark_page_boundary){
			clog << "DEBUG: emitting page rule at page end\n";
			horizontal_rule();
		}
	}

	finish:

	clog << "DEBUG: end of print feeding " << eject_after_print_mm << "mm\n";
	feed_mm(eject_after_print_mm);
	printerInitialise();
	cupsRasterClose(ras);
}

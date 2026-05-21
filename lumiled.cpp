#include "pico/stdlib.h"
#include "hardware/spi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <type_traits>

namespace apa102 {

struct color {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

template <unsigned NumLeds>
class driver {
public:
  static constexpr unsigned num_leds = NumLeds;

  explicit driver(spi_inst_t *spi) noexcept : spi_{spi}{
    // We do not use MISO or CS for APA102
    spi_set_format(
      spi,
      8,         // 8 bits per transfer
      SPI_CPOL_0,     // clock idle low
      SPI_CPHA_0,     // data valid on rising edge
      SPI_MSB_FIRST
    );
  }
  driver(driver const &) = delete;
  driver & operator=(driver const &) = delete;

  void set_brightness(std::uint8_t const brightness) {
    brightness_ = std::min(std::uint8_t{31}, brightness);
  }

  template <std::ranges::input_range Range>
    requires (std::is_same_v<std::remove_cvref_t<color>, std::ranges::range_value_t<Range>> && std::ranges::sized_range<Range>)
  void show(Range const &colors) {
    assert(std::ranges::size(colors) == num_leds);
    this->start_frame();
    for (auto const & color: colors) {
      this->send_led(color);
    }
    this->end_frame();
  }

private:
  spi_inst_t * const spi_;
  std::uint8_t brightness_ = 1U; // brightness: 0–31

  void start_frame() {
    constexpr auto zero = std::uint8_t{0};
    std::array buffer{zero, zero, zero, zero};
    spi_write_blocking(spi_, buffer.data(), buffer.size());
  }

  void end_frame() {
    // At least (num_leds + 15) / 16 bytes of 0xFF
    std::array<std::uint8_t, (num_leds + 15U) / 16U> buffer;
    std::ranges::fill(buffer, std::uint8_t{0xFF});
    spi_write_blocking(spi_, buffer.data(), buffer.size());
  }

  void send_led(color const & c) {
    // First element is the global brightness header
    assert(brightness_ < 32U);
    std::array const frame{static_cast<std::uint8_t>(0b11100000 | brightness_), c.blue, c.green, c.red};
    spi_write_blocking(spi_, frame.data(), frame.size());
  }
};

} // end namespace apa102

namespace {
  
auto black = apa102::color{.red=0, .green=0, .blue=0};
auto red = apa102::color{.red=255, .green=0, .blue=0};
auto yellow = apa102::color{.red=255, .green=255, .blue=0};
auto green = apa102::color{.red=0, .green=255, .blue=0};

}

int main() {
  // Note that these are GPIO number, not pin numbers of the the pins on the 2×20 header.
  constexpr auto pin_sck = 18U;
  constexpr auto pin_mosi = 19U;

  stdio_init_all();

  // Initialize SPI at 1 MHz (APA102 tolerates much faster if needed)
  spi_init(spi0, 1 * 1000 * 1000);
  // Assign SPI functions to GPIO 24/25
  gpio_set_function(pin_sck,  GPIO_FUNC_SPI);
  gpio_set_function(pin_mosi, GPIO_FUNC_SPI);

  constexpr auto num_leds = 20U;   // Lumini ring has 20 LEDs
  apa102::driver<num_leds> driver{spi0};
  std::array<apa102::color, num_leds> leds;
  auto level = 0;
  auto direction = 1;
  while (true) {
    for (auto ctr = 0; ctr < num_leds; ++ctr) {
      auto c = black;
      if (ctr <= level) {
        if      (ctr >= 17) { c = red;    } 
        else if (ctr >= 14) { c = yellow; } 
        else if (ctr >=  4) { c = green;  }
      }
      leds[ctr] = c;
    }
    driver.show(leds);  // brightness 0–31

    level += direction;
    if (level >= num_leds || level < 0) {
      direction = -direction;
    }
    sleep_ms(100);
  }
}

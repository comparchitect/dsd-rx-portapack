/*
 * External wrapper for MBELIB offline decoder.
 */

#include "ui.hpp"
#define MBELIB_USE_PREPARED_IMAGE 1
#include "apps/mbelib_app.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"

namespace ui::external_app::mbelib {
void initialize_app(ui::NavigationView& nav) {
    nav.push<MBELIBView>();
}
}  // namespace ui::external_app::mbelib

extern "C" {

__attribute__((section(".external_app.app_mbelib.application_information"), used)) application_information_t _application_information_mbelib = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::mbelib::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,

    /*.app_name = */ "MBELIB",
    /*.bitmap_data = */ {
        0x00, 0x00, 0x7E, 0x7E, 0x42, 0x66, 0x66, 0x42,
        0x42, 0x66, 0x66, 0x42, 0x7E, 0x7E, 0x42, 0x42,
        0x7E, 0x7E, 0x18, 0x3C, 0x3C, 0x18, 0x3C, 0x7E,
        0x7E, 0x3C, 0x18, 0x3C, 0x7E, 0x7E, 0x00, 0x00,
    },
    /*.icon_color = */ ui::Color::orange().v,
    /*.menu_location = */ app_location_t::UTILITIES,
    /*.desired_menu_position = */ -1,

    /*.m4_app_tag = portapack::spi_flash::image_tag_mbelib_decode */ {'P', 'A', '2', 'D'},
    /*.m4_app_offset = */ 0x00000000,  // filled when packaging
};
}

/*
 * External wrapper for DSD RX.
 */

#include "ui.hpp"
#define DSDRX_USE_PREPARED_IMAGE 1
#include "apps/dsd_app.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"

namespace ui::external_app::dsd_rx {
void initialize_app(ui::NavigationView& nav) {
    nav.push<DSDView>();
}
}  // namespace ui::external_app::dsd_rx

extern "C" {

__attribute__((section(".external_app.app_dsd_rx.application_information"), used)) application_information_t _application_information_dsd_rx = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::dsd_rx::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,

    /*.app_name = */ "DSD RX",
    /*.bitmap_data = */ {
        0x00, 0x00, 0x7E, 0x3C, 0xC3, 0xC3, 0x99, 0x99,
        0xBD, 0xBD, 0xBD, 0xBD, 0x99, 0x99, 0xC3, 0xC3,
        0x3C, 0x7E, 0x18, 0x18, 0x3C, 0x7E, 0x7E, 0x3C,
        0x18, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x00, 0x00,
    },
    /*.icon_color = */ ui::Color::cyan().v,
    /*.menu_location = */ app_location_t::RX,
    /*.desired_menu_position = */ -1,

    /*.m4_app_tag = portapack::spi_flash::image_tag_dsd_rx */ {'P', 'D', 'S', 'D'},
    /*.m4_app_offset = */ 0x00000000,  // filled when packaging
};
}


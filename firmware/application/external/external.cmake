option(WITH_EXTERNAL_MBELIB "Build external MBELIB app" ON)
option(WITH_EXTERNAL_DSDRX "Build external DSD RX app" ON)

set(EXTCPPSRC

	#afsk_rx
	external/afsk_rx/main.cpp
	external/afsk_rx/ui_afsk_rx.cpp

	#calculator
	external/calculator/main.cpp
	external/calculator/ui_calculator.cpp

	#font_viewer
	external/font_viewer/main.cpp
	external/font_viewer/ui_font_viewer.cpp

	#blespam
	external/blespam/main.cpp
	external/blespam/ui_blespam.cpp

	#analogtv
	external/analogtv/main.cpp
	external/analogtv/analog_tv_app.cpp

	#nrf_rx
	external/nrf_rx/main.cpp
	external/nrf_rx/ui_nrf_rx.cpp

	#coasterp
	external/coasterp/main.cpp
	external/coasterp/ui_coasterp.cpp

	#lge
	external/lge/main.cpp
	external/lge/lge_app.cpp

	#lcr
	external/lcr/main.cpp
	external/lcr/ui_lcr.cpp

	#jammer
	# external/jammer/main.cpp  # DISABLED to save flash space
	# external/jammer/ui_jammer.cpp  # DISABLED to save flash space

	#gpssim
	external/gpssim/main.cpp
	external/gpssim/gps_sim_app.cpp

	#spainter
	external/spainter/main.cpp
	external/spainter/ui_spectrum_painter.cpp
	external/spainter/ui_spectrum_painter_text.cpp
	external/spainter/ui_spectrum_painter_image.cpp

	#keyfob
	external/keyfob/main.cpp
	external/keyfob/ui_keyfob.cpp
	external/keyfob/ui_keyfob.hpp

	#tetris
	external/tetris/main.cpp
	external/tetris/ui_tetris.cpp

	#extsensors
	external/extsensors/main.cpp
	external/extsensors/ui_extsensors.cpp
	external/extsensors/ui_extsensors.hpp

	#foxhunt
	external/foxhunt/main.cpp
	external/foxhunt/ui_foxhunt_rx.cpp
	external/foxhunt/ui_foxhunt_rx.hpp

	#audio_test
	external/audio_test/main.cpp
	external/audio_test/ui_audio_test.cpp

	#wardrivemap
	external/wardrivemap/main.cpp
	external/wardrivemap/ui_wardrivemap.cpp

	#tpmsrx
	# external/tpmsrx/main.cpp  # DISABLED to save flash space
	# external/tpmsrx/tpms_app.cpp  # DISABLED to save flash space

	#protoview
	external/protoview/main.cpp
	external/protoview/ui_protoview.cpp

	#adsbtx
	# external/adsbtx/main.cpp  # DISABLED to save flash space
	# external/adsbtx/ui_adsb_tx.cpp  # DISABLED to save flash space

	#morse_tx
	external/morse_tx/main.cpp
	external/morse_tx/ui_morse.cpp

	#sstvtx
	external/sstvtx/main.cpp
	external/sstvtx/ui_sstvtx.cpp

	#random - DISABLED to save flash space
	#external/random_password/main.cpp
	#external/random_password/ui_random_password.cpp
	#external/random_password/sha512.cpp
	#external/random_password/sha512.h

	#acars
	external/acars_rx/main.cpp
	external/acars_rx/acars_app.cpp

	#wefax_rx
	# external/wefax_rx/main.cpp  # DISABLED to save flash space
	# external/wefax_rx/ui_wefax_rx.cpp  # DISABLED to save flash space


	#noaaapt_rx
	external/noaaapt_rx/main.cpp
	external/noaaapt_rx/ui_noaaapt_rx.cpp

	#mbelib offline decoder
	$<$<BOOL:${WITH_EXTERNAL_MBELIB}>:external/mbelib/main.cpp>

	#dsd_rx
	$<$<BOOL:${WITH_EXTERNAL_DSDRX}>:external/dsd_rx/main.cpp>


	#shoppingcart_lock
	external/shoppingcart_lock/main.cpp
	external/shoppingcart_lock/shoppingcart_lock.cpp

	#ookbrute
	external/ookbrute/main.cpp
	external/ookbrute/ui_ookbrute.cpp

	#ook_editor - DISABLED to save flash space
	#external/ook_editor/main.cpp
	#external/ook_editor/ui_ook_editor.cpp

	#cvs_spam
	external/cvs_spam/main.cpp
	external/cvs_spam/cvs_spam.cpp

	#flippertx
	external/flippertx/main.cpp
	external/flippertx/ui_flippertx.cpp

	#remote - DISABLED to save flash space
	#external/remote/main.cpp
	#external/remote/ui_remote.cpp

	#mcu_temperature
	external/mcu_temperature/main.cpp
	external/mcu_temperature/mcu_temperature.cpp

	#fmradio
	# external/fmradio/main.cpp  # DISABLED to save flash space
	# external/fmradio/ui_fmradio.cpp  # DISABLED to save flash space

	#tuner
	external/tuner/main.cpp
	external/tuner/ui_tuner.cpp

	#metronome
	external/metronome/main.cpp
	external/metronome/ui_metronome.cpp

	#app_manager
	external/app_manager/main.cpp
	external/app_manager/ui_app_manager.cpp

	#hopper
	# external/hopper/main.cpp  # DISABLED to save flash space
	# external/hopper/ui_hopper.cpp  # DISABLED to save flash space

	# whip calculator
	external/antenna_length/main.cpp
	external/antenna_length/ui_whipcalc.cpp

	# wav viewer
	external/wav_view/main.cpp
	external/wav_view/ui_view_wav.cpp

	# wipe sdcard
	external/sd_wipe/main.cpp
	external/sd_wipe/ui_sd_wipe.cpp

	# playlist editor
	# external/playlist_editor/main.cpp  # DISABLED to save flash space
	# external/playlist_editor/ui_playlist_editor.cpp  # DISABLED to save flash space

	#snake
	external/snake/main.cpp
	external/snake/ui_snake.cpp

	#stopwatch
	external/stopwatch/main.cpp
	external/stopwatch/ui_stopwatch.cpp

	#breakout
	external/breakout/main.cpp
	external/breakout/ui_breakout.cpp

	#dinogame - DISABLED to save flash space
	#external/dinogame/main.cpp
	#external/dinogame/ui_dinogame.cpp

	#doom - DISABLED to save flash space
	#external/doom/main.cpp
	#external/doom/ui_doom.cpp

	#debug_pmem - DISABLED to save flash space
	#external/debug_pmem/main.cpp
	#external/debug_pmem/ui_debug_pmem.cpp

	#scanner - DISABLED to save flash space
	#external/scanner/main.cpp
	#external/scanner/ui_scanner.cpp

	#level
	# external/level/main.cpp  # DISABLED to save flash space
	# external/level/ui_level.cpp  # DISABLED to save flash space

	#gfxEQ
	external/gfxeq/main.cpp
	external/gfxeq/ui_gfxeq.cpp

	#detector_rx
	external/detector_rx/main.cpp
	external/detector_rx/ui_detector_rx.cpp

	#space_invaders
	# external/spaceinv/main.cpp  # DISABLED to save flash space
	# external/spaceinv/ui_spaceinv.cpp  # DISABLED to save flash space

	#blackjack - DISABLED to save flash space
	#external/blackjack/main.cpp
	#external/blackjack/ui_blackjack.cpp

	#battleship - DISABLED to save flash space
	#external/battleship/main.cpp
	#external/battleship/ui_battleship.cpp

	#ert
	external/ert/main.cpp
	external/ert/ert_app.cpp

	#epirb_rx - DISABLED to save flash space
	#external/epirb_rx/main.cpp
	#external/epirb_rx/ui_epirb_rx.cpp

	#soundboard
	# external/soundboard/main.cpp  # DISABLED to save flash space
	# external/soundboard/soundboard_app.cpp  # DISABLED to save flash space

	#game2048
	# external/game2048/main.cpp  # DISABLED to save flash space
	# external/game2048/ui_game2048.cpp  # DISABLED to save flash space

	#bht_tx
	external/bht_tx/main.cpp
	external/bht_tx/ui_bht_tx.cpp
	external/bht_tx/bht.cpp
)

set(EXTAPPLIST
	afsk_rx
	calculator
	font_viewer
	blespam
	nrf_rx
	analogtv
	coasterp
	lge
	lcr
	# jammer  # DISABLED to save flash space
	gpssim
	spainter
	keyfob
	tetris
	extsensors
	foxhunt_rx
	audio_test
	wardrivemap
	cvs_spam
	# tpmsrx  # DISABLED to save flash space
	protoview
	adsbtx
	morse_tx
	sstvtx
	#random_password - DISABLED to save flash space
	# acars_rx
	ookbrute
	ook_editor
	wefax_rx
	noaaapt_rx
	shoppingcart_lock
	flippertx
	#remote - DISABLED to save flash space
	mcu_temperature
	fmradio
	tuner
	metronome
	app_manager
	# hopper  # DISABLED to save flash space
	antenna_length
	view_wav
	sd_wipe
	# playlist_editor  # DISABLED to save flash space
	snake
	stopwatch
	breakout
	#dinogame - DISABLED to save flash space
	#doom - DISABLED to save flash space
	#debug_pmem - DISABLED to save flash space
	#scanner - DISABLED to save flash space
	level
	gfxeq
	detector_rx
	# spaceinv  # DISABLED to save flash space
	#blackjack - DISABLED to save flash space
	#battleship - DISABLED to save flash space
	ert
	epirb_rx
	# soundboard  # DISABLED to save flash space
	# game2048  # DISABLED to save flash space
	bht_tx
	$<$<BOOL:${WITH_EXTERNAL_MBELIB}>:mbelib>
	$<$<BOOL:${WITH_EXTERNAL_DSDRX}>:dsd_rx>
)

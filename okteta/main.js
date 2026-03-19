function init(){
	const samples = 128;
	var adsample = struct({
		data     : uint16(),
		dummy    : uint16()
	});
	adsample.byteOrder = "big-endian";

	var adsamples = array(adsample, samples);
	//adsamples.byteOrder = "big-endian";
//	adsamples.byteOrder = "big-endian";

	var record = struct({
		usecs    : uint32(),
//		samples  : array(uint32(), 1024),
		datasamples  : adsamples,
		gpiomask : array(uint32(), samples)
	});

	var mvaring = struct({
		version  : uint8(),
		writing  : uint8(),
		dropped  : uint16(),
		size     : uint32(),
		rindex   : uint32(),
		windex   : uint32(),
		adcdata : array(record, 201) //FIXME: size hardcoded, no length in format, read until EOF
	});
	mvaring.defaultLockOffset = 0;

	return mvaring;
}

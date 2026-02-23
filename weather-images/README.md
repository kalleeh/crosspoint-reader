# Weather Background Images

CrossPoint Reader supports custom weather background images stored on the SD card.

## Installation

1. Copy all `.bmp` files from this folder to your device via the web UI
2. Upload them to the `/.crosspoint/weather/` directory on your SD card
3. The weather app will automatically use them as backgrounds

## Location on Device

Weather images must be stored at: `/.crosspoint/weather/` on the SD card

## Supported Images

The following BMP files are supported:

- `clear.bmp` - Clear/sunny weather
- `cloudy.bmp` - Cloudy/overcast weather
- `rainy.bmp` - Rain/drizzle
- `snowy.bmp` - Snow
- `stormy.bmp` - Thunderstorms
- `partly-cloudy.bmp` - Partly cloudy

## Image Specifications

- **Format**: BMP (24-bit or 8-bit grayscale)
- **Resolution**: 480×800 pixels (native screen resolution)
- **Color**: Grayscale (16 levels for best e-ink rendering)
- **File size**: ~165KB per image

## Fallback Behavior

If weather images are not found on the SD card, the app will fall back to simple drawn icons.

## Customization

You can replace the default images with your own:

1. Create 480×800 pixel grayscale images
2. Convert to BMP format
3. Name them according to the list above
4. Upload to `/.crosspoint/weather/` via web UI

## Technical Notes

- Images are loaded from SD card (zero flash usage)
- Text is overlaid on top of background images
- Images should have medium-light tones for text readability
- E-ink displays have 16 grayscale levels - avoid complex gradients

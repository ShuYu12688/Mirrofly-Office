"""Generate original TIFF decoder fixtures with Pillow; not needed at runtime."""

from pathlib import Path

from PIL import Image, TiffImagePlugin


def main():
    directory = Path(__file__).resolve().parent
    image = Image.new('RGB', (8, 8), 'red')
    image.paste('blue', (4, 0, 8, 8))
    image.save(directory / 'presentation-rgb.tiff', compression='tiff_lzw')

    oriented = Image.new('RGB', (2, 3))
    oriented.putdata([(255, 0, 0), (0, 0, 255), (0, 255, 0),
                      (255, 255, 0), (0, 255, 255), (255, 0, 255)])
    for orientation in range(2, 9):
        tags = TiffImagePlugin.ImageFileDirectory_v2()
        tags[274] = orientation
        oriented.save(directory / f'presentation-orientation-{orientation}.tiff',
                      tiffinfo=tags, compression='raw')

    large = Image.new('RGB', (4096, 8), 'red')
    large.paste('blue', (2048, 0, 4096, 8))
    large.save(directory / 'presentation-large.tiff', compression='tiff_lzw')


if __name__ == '__main__':
    main()

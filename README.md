mandelbrot-explorer
===================

mandelbrot explorer

I would add a warning at this point... if you have an Apple CPU, avoid this... it crashes Metal on MacOS 15.x, after converting
the kernel to use floats, I was able to get a working kernel... but I had to reboot the system to reload Metal after it crashed.


Its a toy, but it uses OpenCL to render with. Its fast has a bunch of options for zooming / quality / renderer type.

The default renderer is OpenCL but it can use the double or bignum rendereres both are spread across multiple CPU threads.

You can control the quality and depth of the image by changing the max / min iterations.

Zooming and point and click work fine, zooming out just takes you to the previous level with no rendering required.


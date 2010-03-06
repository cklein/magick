
# Examples from the PythonMagick tutorial
# In general, ImageMagick commands that altered the images in place
#   are implemented as methods on the image object.
# While commands that returned a new image are methods of magick
#   and can take any object that can be converted to an image as
#   the image argument. 

import magick
from Numeric import *
import Numeric

def load_save():
    img = magick.image('logo:')
    img.write('newfile.png')
    img.filename='anotherway.png'
    img.write()

def test_resize():
    half = magick.minify('logo:')  #reduce by factor of 2
    double = magick.magnify(half)  # back to the same size (blurred image)
    # resize also accepts a blur factor and a filter type argument.
    newsize = magick.resize('logo:',(200,200)) # change aspect ratio
    shrink = magick.resize('logo:',(200,-1)) # keep aspect ratio
    expand = magick.resize('logo:',(-1,1.3)) # expand 1.3 times
    # I could also use sample, scale, or thumbnail which use different
    #  arguments and use different methods to do the resizing.
    # Thumbnail is similar to resize, but without blur and method arguments
    # Sample and scale do not understand floating point values as
    #  resizing factors.
    #b = magick.image("testimages/input.m2v")  # a movie sequence.
    #len(b)  # should be 6
    #element = b[0]   # extract a piece

def effects():
    img = magick.image('testimages/original.jpg')
    new = magick.blur(img,3,1.5)
    new = magick.blur('testimages/original.jpg',3,1.5)  #also works
    new = magick.rotate(img,20)
    new = img.copy()
    new.contrast(10)
    out = magick.border(img,6,6,bordercolor='red')
    out = magick.charcoal(img,1,0.5)
    out = magick.colorize(img, 'red', 0.30)

def composition():
    img = magick.image('testimages/original.jpg')
    small = magick.minify(img)
    small.opacity = 0.3 * magick.iMaxRGB
    img.composite(small, 5, 5, 'over')
    return img

# Animations are simply image sequences with more than one 

def animate1():
    img = magick.scale('testimages/original.jpg',(100,-1))
    img2 = magick.blur(img, 5, 1.5)
    imgs = magick.image(img, img2)
    imgs.write('ani1.gif')
    return 

def createFrame(val):
    val2 = 2.0*pi*val/360
    radius = 26
    x = cos(val2)*radius
    y = sin(val2)*radius
    # uses the savespace property of Numeric arrays to be sure
    #   that the output of multiplication by maxRGB stays the same type.
    #   MaxRGB is actually a rank-0 Numeric savespace array of the
    #   quantum type. 
    img = magick.image(Numeric.ones((60,60,3)) * magick.MaxRGB)
    dc = magick.newdc()
    dc.fill = 'white'
    dc.stroke = 'red'
    dc.stroke_width = 3
    dc.circle(30,30,radius)
    img.draw(dc)
    dc.stroke = 'blue'
    dc.line(30,30,30+x,30+y)
    img.draw(dc)
    return img
    
def animate2():
    images = magick.image(*[createFrame(x) for x in range(0,360,10)])
    images.write('clock.gif')
    return images
        
def draw_shapes():
    img = magick.image('xc:#ffffffff',size='150x100')  # 'xc:transparent' should also work
    dc = magick.newdc()   # new "Drawing context with ImageMagick defaults"
    #dc.text(10,10,'some drawings ...')
    #img.draw(dc)
    dc.stroke='blue'
    dc.fill='none'
    dc.stroke_width = 3
    dc.circle(50,50,10) # radius of 10
    img.draw(dc)  # draw the primitives and clear them (keeps the state)
    dc.stroke='red'
    dc.rect(140,90,110,60)
    dc.line(110,60,125,40)
    dc.line(140,60,125,40)
    img.draw(dc)
    coords = [[10,90],[120,90],[70,20],[140,10]]  # can also be 1-D
    dc.bezier(coords)
    dc.stroke='green'
    img.draw(dc)
    return img

def draw_bar():
    data = [('red',30),('blue',80),('green',60)]
    img = magick.image('xc:white',size='100x150')
    dc = magick.newdc()
    dc.stroke = 'black'
    dc.stroke_width = 1
    left = 10
    bottom = 90
    size = 20
    for color,height in data:
        dc.fill = color
        dc.rect(left,bottom,left+size,bottom-height)
        img.draw(dc)
        left += size+2
    return img;


# Inspiration for the following examples taken from Rmagick site:
#   in the Drawing Demonstration.

def make_hatch(width, height, background='white', color='lightcyan2', sp=10):
    hatch = Numeric.ones((height, width,3)) * magick.MaxRGB
    val = magick.name2color(color)
    hatch[:,:,:] = magick.name2color(background)[:-1]
    hatch[sp::sp,:,:] = val[:-1]
    hatch[:,sp::sp,:] = val[:-1]
    img = magick.border(hatch, sp, sp, bordercolor=background)
    return img
                    
def draw_graph():
    img = make_hatch(240,300)
    gc = magick.newdc()
    gc.stroke='red'
    gc.stroke_width = 3
    gc.fill='none'
    gc.ellipse(120,150,80,120,0,270)
    img.draw(gc)

    gc.stroke='gray50'
    gc.stroke_width=1
    gc.circle(120,150,4)
    gc.circle(200,150,4)
    gc.circle(120,30,4)

    gc.line(120,150,200,150)
    gc.line(120,150,120,30)
    img.draw(gc)

    gc.stroke='transparent'
    gc.fill = 'black'
    #gc.set_font('/Users/cklein/Library/Fonts/Topaz-8.ttf')
    #gc.text(130,35,"End")
    #gc.text(188, 135, "Start")
    #gc.text(130, 95, "Height=120")
    #gc.text(55,155, "Width=80")
    img.draw(gc)
    return img

def draw_bezier():
    img = make_hatch(240,300)
    gc = magick.newdc()
    # Draw curve
    gc.stroke='blue'
    gc.stroke_width = 3
    gc.fill = 'none'
    gc.bezier([45,150, 45,20, 195,280, 195,150])
    img.draw(gc)
    
    # Draw endpoints
    gc.stroke='gray50'
    gc.stroke_width=1
    gc.circle(45,150,4)
    gc.circle(195,150,4)
    img.draw(gc)
    
    # Draw control points
    gc.fill = 'gray50'
    gc.circle(45,17,4)
    gc.circle(195,280,4)
    
    # Connect the points
    gc.line(45,150, 45,17)
    gc.line(195,280, 195,150)
    img.draw(gc)
    
    # Annotate
    gc.stroke='transparent'
    gc.fill= 'black'
    #gc.text(27, 175, "45,150")
    #gc.text(175,138, "195,150")
    #gc.text(55,22, "45,20")
    #gc.text(143,285, "195,280")
    img.draw(gc)
    return img

def draw_svgpath():
    img = make_hatch(240,300)
    gc = magick.newdc()

    gc.fill = 'red'
    gc.stroke = 'blue'
    gc.stroke_width = 2
    gc.path("M120,150 h-75 a75,75 0 1, 0 75, -75 z")
    img.draw(gc)

    gc.fill = 'yellow'
    gc.path("M108.5,138.5 v-75 a75, 75 0 0,0 -75, 75 z")
    img.draw(gc)
    return img

def draw_poly():
    img = make_hatch(240,300)
    gc = magick.newdc()

    gc.stroke = "#001aff"
    gc.stroke_width = 3
    gc.fill = "#00ff00"
    x = 120
    y = 32.5
    gc.polygon([[x,y],
                [x+29,y+86],
                [x+109,y+86],
                [x+47, y+140],
                [x+73, y+226],
                [x, y+175],
                [x-73, y+226],
                [x-47, y+140],
                [x-109, y+86],
                [x-29, y+86]])
    img.draw(gc)
    return img

if __name__ == "__main__":
    load_save()
    test_resize()
    effects()
    composition() #.display()
    animate1()
    animate2() #.animate()
    draw_shapes() #.display()
    draw_bar() #.display()
    img = draw_graph() #.display()
    img.write("draw_graph.png")
    draw_bezier() #.display()
    draw_svgpath() #.display()
    draw_poly() #.display()
    
    

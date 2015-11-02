# test the malis of boundary map
import emirt
import numpy as np
import time
import utils
import matplotlib.pylab as plt
#%% parameters
z = 8
# epsilone: a small number for log to avoind -infinity
eps = 0.0000001

# largest disk radius
Dm = 500
Ds = 500

# make a fake test image
is_fake = True

# whether using constrained malis
is_constrained = False

# thicken boundary of label by morphological errosion
erosion_size = 0

# a small corner
corner_size = 0

# disk radius threshold
DrTh = 0

#%% read images
if not is_fake:
    bdm = emirt.emio.imread('../experiments/zfish/VD2D/out_sample91_output_0.tif')
    lbl = emirt.emio.imread('../dataset/zfish/Merlin_label2_24bit.tif')
    raw = emirt.emio.imread('../dataset/zfish/Merlin_raw2.tif')
    lbl = emirt.volume_util.lbl_RGB2uint32(lbl)
    lbl = lbl[z,:,:]
    bdm = bdm[z,:,:]
else:
    # fake image size
    fs = 20
    bdm = np.ones((fs,fs), dtype='float32')
    bdm[3,:] = 0.5
    bdm[3,7] = 0.8
    bdm[3,3] = 0.2
    bdm[6,:] = 0.5
    bdm[6,3] = 0.2
    bdm[6,7] = 0.8
    lbl = np.zeros((fs,fs), dtype='uint32')
    lbl[:6, :] = 1
    lbl[7:, :] = 2
assert lbl.max()>1

# only a corner for test
if corner_size > 0:
    lbl = lbl[:corner_size, :corner_size]
    bdm = bdm[:corner_size, :corner_size]

# fill label holes
print "fill boundary hole..."
utils.fill_boundary_holes( lbl )

# increase boundary width
if erosion_size>0:
    print "increase boundary width"
    erosion_structure = np.ones((erosion_size, erosion_size))
    msk = np.copy(lbl>0)
    from scipy.ndimage.morphology import binary_erosion
    msk = binary_erosion(msk, structure=erosion_structure)
    lbl[msk==False] = 0

# recompile and use cost_fn
#print "compile the cost function..."
#os.system('python compile.py cost_fn')
import cost_fn
start = time.time()
if is_constrained:
    print "compute the constrained malis weight..."
    w, me, se = cost_fn.constrained_malis_weight_bdm_2D(bdm, lbl)
else:
    print "compute the normal malis weight..."
    w, me, se = cost_fn.malis_weight_bdm_2D(bdm, lbl)

elapsed = time.time() - start
print "elapsed time is {} sec".format(elapsed)


#%% plot the results
print "plot the images"
if is_constrained:
    mbdm = np.copy(bdm)
    mbdm[lbl>0] = 1
    plt.subplot(241)
    plt.imshow(mbdm, cmap='gray', interpolation='nearest')
    plt.xlabel('merger constrained boundary map')

    sbdm = np.copy(bdm)
    sbdm[lbl==0] = 0
    plt.subplot(245)
    plt.imshow(sbdm, cmap='gray', interpolation='nearest')
    plt.xlabel('splitter constrained boundary map')
else:
    plt.subplot(241)
    plt.imshow(bdm, cmap='gray', interpolation='nearest')
    plt.xlabel('boundary map')
    plt.subplot(245)
#    plt.imshow(lbl>0, cmap='gray', interpolation='nearest')
    emirt.show.random_color_show( lbl, mode='mat' )
    plt.xlabel('manual labeling')

# rescale to 0-1
def rescale(arr):
    arr = arr - arr.min()
    arr = arr / arr.max()
    return arr

def combine2rgb(bdm, w=None):
    # make a combined colorful image
    cim = np.zeros(bdm.shape+(3,), dtype='uint8')
    # red channel
    cim[:,:,0] = (rescale(bdm))*255
    # green channel
    if w is not None:
        cim[:,:,1] = rescale( w )*255
    return cim

def disk_plot(e, D, DrTh, color='g'):
    # plot disk to illustrate the weight strength
    # rescale to 0-1
    re = rescale(e) * D
    y,x = np.nonzero(re)
    r = re[(y,x)]
    # sort the disk from small to large
    locs = np.argsort( r )
    y = y[ locs ]
    x = x[ locs ]
    r = r[ locs ]
    # eleminate the small disks
    y = y[ r > DrTh ]
    x = x[ r > DrTh ]
    r = r[ r > DrTh ]
    plt.scatter(x,y,r, c=color, alpha=0.8, linewidths=0)


# combine merging error with boundary map
rgbm = combine2rgb(1-bdm, np.log(me+eps))
plt.subplot(242)
plt.imshow( rgbm, interpolation='nearest' )
plt.xlabel('combine boundary map(red) and ln(merge weight)(green)')

# combine merging error with boundary map
rgbs = combine2rgb(1-bdm, np.log(se+eps))
plt.subplot(246)
plt.imshow( rgbs, interpolation='nearest' )
plt.xlabel('combine boundary map(red) and ln(split weight)(green)')

# combine merging error with boundary map
rgb_bdm = combine2rgb(1-bdm)
plt.subplot(243)
plt.imshow( rgb_bdm, interpolation='nearest' )
plt.xlabel('combine boundary map(red) \n and merge weight(green disk)')
disk_plot(me, Dm, DrTh)

plt.subplot(247)
plt.imshow( rgb_bdm, interpolation='nearest' )
plt.xlabel('combine boundary map(red) \n and split weight(green disk)')
disk_plot(se, Ds, DrTh)

# gradient
# square loss gradient
grdt = 2 * (bdm-  (lbl>0).astype('float32'))
# merger and splitter gradient
mg = grdt * me
sg = grdt * se

plt.subplot(244)
#cim,mpg,mng = gradient2rgb(mg)
mgcim = combine2rgb(1-bdm)
plt.imshow(mgcim, interpolation='nearest')
disk_plot( np.abs(mg), Dm, DrTh, color='g')
plt.xlabel('merger gradient (square loss absolute value)')


plt.subplot(248)
#cim,spg,sng = gradient2rgb(sg)
sgcim = combine2rgb(1-bdm)
plt.imshow(sgcim, interpolation='nearest')
disk_plot( np.abs(sg), Ds, DrTh, color='g' )
plt.xlabel('splitter gradient (square loss absolute value)')

plt.show()

print "------end-----"

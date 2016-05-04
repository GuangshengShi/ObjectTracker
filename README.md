# ObjectTracker

## Usage
The main object tracker runs in three modes:
* tracker = Track objects and optionally put the tracked objects in a tracking file
* plotter = Use a file with estimated positions and plot the dots on the video
* annotater = Play video and record ground truth

To run the object tracker first create a directory called `build/` at the project root.

Now, run `start.sh`. This takes in the following command line arguments:

* `-i <path_to_input_video>`
* `-m <mode>` - The mode should be either `tracker`, `plotter`, or `annotater`
* `-p <x1 y1 x2 y2 x3 y3 x4 y4>` (optional) - Applies a perspective transform using the four given points
* `-d <maxSize>` (optional) - Scales the video so that neither the height nor width of the video exceeds maxSize pixels
* `-s <path_to_support_file` (optional) - If you're in tracker mode, this is where the program will output tracking data. If you're in plotter mode, this is the path to the CSV file with the estimated positions. If you're in annotater mode, this is where the ground truth data will be written.

For example, to run the plotter with a max size and perspective transform, you may do something like this:

```
./start.sh -m plotter -i ~/myvideo.mov -p 12 123 212 56 12 124 51 213 -d 500 -s ~/myestimatedpositions.csv
```

### Preprocessing Scripts
You likely will have to preprocess your data to use it with the tracker. Here are the preprocessing scripts.

* `evaluator.py` - Ignore this for now.
* `timestamp_to_frame.py` - Use this before you use the plotter. It takes a file with `(timestamp, tracker_id, x, y)` tuples and turns it into a CSV with `(timestamp, x, y, frame)` tuples. Run it as follows
  * `python timestamp_to_frame.py <estimated_positions_file> <num_frames> <start_ts> <duration>`  
* `trajectory_smoother.py` - Use after running tracker. This will smooth the trajectories and also compute velocities and accelerations. Run it as follows: 
  * `python trajectory_smoother.py <path_to_tracker_output_JSON_file> <start_ts> <duration>`

### Video Metadata
The starting timestamps, durations, and perspective points for the videos. You can find this information in `run_tracker.py` - see the dictionary called `data`.

## Ignore the stuff below for now

## Note
This pipeline is still very much a prototype, so please keep that in mind if you'd like to use it. I am still learning computer vision, so I'd appreciate any suggestions on how to improve this pipeline's tracking ability or make it simpler (while maintaining similar performance). Thank you!

This README describes how the tracking pipeline works. I hope it can be helpful to you if you decide to build your own tracking pipeline.

## Acknowledgements
I want to give credit to:

This [paper](http://ceur-ws.org/Vol-1391/40-CR.pdf) by Gábor Szűcs, Dávid Papp, and Dániel Lovas, which outlines the Kalman + Hungarian tracking pipeline.

This [repo](https://github.com/Smorodov/Multitarget-tracker) by Andrey Smorodov, which implements the Kalman + Hungarian pipeline.
I use his code for the Hungarian algorithm and his technique for initializing the matrices for Kalman filters.

This [website](http://web.rememberingemil.org/Projects/DisjointSets.aspx.html) by Emil Stefanov, 
which has an implementation of the union-find (aka. disjoint set) data structure.

[OpenCV](http://opencv.org/), which does most of the heavy lifting for the computer vision tasks that I do.

## Demo
Here are some sample videos showing how the tracker works. The videos are taken from computer vision datasets.

[One person](https://youtu.be/czICDnhEqRo)

[Multiple people](https://youtu.be/uumkLHkNZ2Y)

[Multiple people 2](https://youtu.be/RMJiSz4aK_8)

## Intro
This is an object tracking pipeline that I built. It is designed for high mounted fixed cameras 
to track objects moving in the scene below them. So, I'm making two important assumptions here:

* The camera is not moving
* It has an overhead (or almost overhead) view of the moving objects

Here's an outline of its operation, with explanations of why these decisions were made.

## Resize Image
Computing on large images is expensive, so I first shrink each frame down to **(300, 300)** pixels.

## Background Subtraction
To separate the foreground (the moving objects) from the backgound, I use background subtraction
with a [Mixture of Gaussians](http://docs.opencv.org/2.4/modules/video/doc/motion_analysis_and_object_tracking.html#backgroundsubtractormog2).
This basically models the probability that a given pixel will change intensity in the next frame.
So, pixels that are very likely to change intensity are the foreground.

I also use this model to **detect shadows** and threshold them out.

## Preprocessing
To **remove salt and pepper noise**, I use a **median blur**. This, along with the removal of shadows, can lead
to **holes in the image**, so I fill those in by **dilating** the image a few times.

## Contours

### First Pass
Now, I find the **contours** in the image. It's possible that **contours are drawn around noise**, so I address that
by removing all contours that have an **area that is less than 10% of the largest contour's area**. Then, I compute the **center of mass** and **bounding box** around each of the remaining contours.

### Merging
Sometimes, an object **may appear as two small pieces, rather than one whole**. So, I want to merge contours that
are near each other. To do this, I have to first explain what it means for contours to be near each other.

To figure out if two contours are near each other, I first compute the **distance between their mass centers**, call it `dist`.
Then, I look at the bounding boxes, call them `b1` and `b2`. Specifically, I decide to merge the two contours if:

```
dist <= 0.7 * max(max(b1.width, b1.height), max(b2.width, b2.height))
```

I keep track of contours that should be merged using the **union find** data structure.

To merge a set of contours, I just **aggregate their points** together to make one big contour.

### Second Pass
After the merging process, I have the final set of contours. So, I **recompute the bounding boxes and mass centers**.

## Multiple Object Tracking
Now, I feed these mass centers and bounding boxes to the multiple object tracker.

At a high level, the multi-tracker basically **associates a Kalman filter to track each moving object**.
There's a challenge here though, which is that the **mass centers for a given frame don't tell me which object they
belong to**. That's something I need to address. But first...

### Base Cases
If there are **no mass centers**, I just **update each Kalman filter** (using their most recent observation).

If there are some mass centers, but **no Kalman filters**, I **create one for each mass center**.

### Hungarian Algorithm
Since the two base cases have been handled, now I can be sure that I have some Kalman filters and some mass centers.

So, the problem is: **how should mass centers be paired with Kalman filters?**

To make this happen, I use the **[Hungarian Algorithm](https://en.wikipedia.org/wiki/Hungarian_algorithm)**. This will
assign Kalman filters to mass centers. The **cost matrix uses the distance** between the Kalman filter and mass center.

### Distance Filter
The assignment found by the Hungarian Algorithm may **assign a Kalman filter with a mass center that is very far away**.
This happens because all the other mass centers are already paired up, so the Kalman filter is forced to pair with a
faraway mass center.

To address this problem, I iterate and **unpair a Kalman filter and mass center if they are too far away**.
I define "too far away" as 

```
distance(Kalman filter, mass center) > (frame.width + frame.height)/2
```

### Dead Trackers
When a **Kalman filter's object has left the screen**, I don't want that Kalman filter sticking around, so
I remove any Kalman filters that have **gone more than 10 frames without being paired with a mass center**.

### Unassigned Mass Centers
Some mass centers **may not have been paired** with a Kalman filter, so I create Kalman filters for them.

### Kalman Update
Now, I **update each Kalman filter** with its paired mass center or with the most recent mass center it has seen.

### Ignore Young Filters
Sometimes, if there's a **patch of noise that persists many frames**, a Kalman filter may be created for it. However,
such Kalman filters usually don't live long, so I only pay attention to Kalman filters that **have been alive for 20 frames**.

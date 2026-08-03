/* [Global Dimensions] */
$fn = 60;                  // Hole smoothness
box_width = 180;           // X-axis outer width
box_length = 150;          // Y-axis outer length
box_height = 55;           // Z-axis overall height
wall_thickness = 3.0;      // Rugged 3mm wall thickness
corner_radius = 8;         // Smooth outer corner aesthetic

/* [M3 Heat-Set Insert Specs] */
insert_hole_d = 4.5;       // Standard pocket diameter for M3 brass knurled inserts
insert_boss_d = 12;       // Thick outer wall to support thermal insertion plastic reflow

/* [Display Configuration & Screw Mounts] */
screen_w = 125;          // Viewable screen width
screen_l = 74;           // Viewable screen height
display_total_w = 143.5;   // Outer glass footprint
display_total_l = 92.0;
display_recess_depth = 1.5;

// Raspberry Pi Touch Display 2 mechanical mounting hole dimensions
display_screw_pitch_x = 134.0;
display_screw_pitch_y = 82.0;

/* [Component Holes] */
footswitch_d = 12.2;       // 12mm guitar pedal footswitch
footswitch_margin = 18;    // Inset from corners
jack_d = 10.0;             // 1/4" Chassis Jack
dc_jack_d = 12.0;          // 12mm DC Barrel Socket
encoder_d = 7.5;           // Rotary Encoder shaft
usb_c_w = 13.0;
usb_c_h = 6.5;

jack_35_d = 6.5;               // Measure your jack's threaded barrel
jack_35_y = box_length / 2;    // Position along the left wall
jack_35_z = box_height / 2;    // Height above the bottom


main_chassis();
//bottom_lid();

module rounded_box(w, l, h, r) {
    linear_extrude(height = h) {
        hull() {
            translate([r, r, 0]) circle(r=r);
            translate([w-r, r, 0]) circle(r=r);
            translate([r, l-r, 0]) circle(r=r);
            translate([w-r, l-r, 0]) circle(r=r);
        }
    }
}

// Maps the 4 corner layout for the bottom lid closure screws
module lid_screw_offsets() {
    inset = 8;
    translate([inset, inset, 0]) children();
    translate([box_width - inset, inset, 0]) children();
    translate([inset, box_length - inset, 0]) children();
    translate([box_width - inset, box_length - inset, 0]) children();
}

// ====================================================================
// CHASSIS
// ====================================================================
module main_chassis() {
    difference() {
        // Core Shell
        rounded_box(box_width, box_length, box_height, corner_radius);

        // Interior Core Carve-out
        translate([wall_thickness, wall_thickness, -1])
            rounded_box(box_width - 2*wall_thickness, box_length - 2*wall_thickness, box_height - wall_thickness + 1, max(1, corner_radius - wall_thickness));

        // TOP PANEL: Display Window
        translate([(box_width/2 - screen_w/2) + 4, box_length/2 - screen_l/2, box_height - wall_thickness - 1])
            cube([screen_w, screen_l, wall_thickness + 2]);

        // TOP PANEL: Corner Footswitches
        translate([footswitch_margin, footswitch_margin, box_height - wall_thickness - 1]) cylinder(d=footswitch_d, h=wall_thickness + 2);
        translate([box_width - footswitch_margin, footswitch_margin, box_height - wall_thickness - 1]) cylinder(d=footswitch_d, h=wall_thickness + 2);
        translate([footswitch_margin, box_length - footswitch_margin, box_height - wall_thickness - 1]) cylinder(d=footswitch_d, h=wall_thickness + 2);
        translate([box_width - footswitch_margin, box_length - footswitch_margin, box_height - wall_thickness - 1]) cylinder(d=footswitch_d, h=wall_thickness + 2);


        // Encoder hole
        translate([box_width/2, box_length - footswitch_margin, box_height - wall_thickness]) cylinder(d=encoder_d, h=wall_thickness + 4);

        // REAR WALL I/O
        translate([0, box_length + 1, 0]) {
            rotate([90, 0, 0]) {
                // audio jacks
                translate([22, box_height/4, 0]) cylinder(d=jack_d, h=wall_thickness + 4);
                translate([22, (box_height/4)*2.5, 0]) cylinder(d=jack_d, h=wall_thickness + 4);

                translate([box_width - 22, box_height/4, 0]) cylinder(d=jack_d, h=wall_thickness + 4);
                translate([box_width - 22, (box_height/4)*2.5, 0]) cylinder(d=jack_d, h=wall_thickness + 4);


                translate([box_width - 44, (box_height/4)*2.5, 0]) cylinder(d=6, h=wall_thickness + 4);

                // DC Power
                translate([box_width/2, box_height/2, 0]) cylinder(d=dc_jack_d, h=wall_thickness + 4);
            }
        }

        // RIGHT WALL: 3.5mm audio jack
        translate([box_width + 1, jack_35_y, jack_35_z])
            rotate([0, -90, 0])
                cylinder(
                    d = jack_35_d,
                    h = wall_thickness + 2
                );

    }




    }



// MOUNTING CORNER BOSSES (For M3 Heated Inserts)
lid_screw_offsets() {
    difference() {
        cylinder(d=insert_boss_d, h=15);
        translate([0, 0, -1]) cylinder(d=insert_hole_d, h=10);
    }
}


module bottom_lid() {
    difference() {
        rounded_box(box_width, box_length, wall_thickness, corner_radius);
        lid_screw_offsets() {
            // Main M3 Bolt Pass-through thread hole
            translate([0, 0, -1]) cylinder(d=3.4, h=wall_thickness + 2);
            // Counterbore pocket: Keeps standard M3 socket head bolts flush with the bottom floor
            translate([0, 0, wall_thickness - 2.0]) cylinder(d=6.5, h=2.1);
        }
    }
}

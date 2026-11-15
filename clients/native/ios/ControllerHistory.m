#import "ControllerHistory.h"
#import <math.h>
static CGFloat bottom(UIScrollView *scroll) {
    return MAX(-scroll.adjustedContentInset.top,
        scroll.contentSize.height-scroll.bounds.size.height+scroll.adjustedContentInset.bottom);
}
BOOL ControllerHistoryAtBottom(UIScrollView *scroll) {
    [scroll layoutIfNeeded];
    return !scroll.dragging && !scroll.decelerating && bottom(scroll)-scroll.contentOffset.y <= 24;
}
void ControllerHistoryFollow(UIScrollView *scroll, BOOL follow) {
    if (!follow) return;
    // Updating the label invalidates Auto Layout. Resolve the new content size
    // before finding the bottom; the previous size would stop one update short.
    [scroll setNeedsLayout];
    [scroll.superview layoutIfNeeded];
    [scroll layoutIfNeeded];
    [scroll setContentOffset:CGPointMake(scroll.contentOffset.x,bottom(scroll)) animated:NO];
}

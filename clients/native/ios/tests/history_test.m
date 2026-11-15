#import "ControllerHistory.h"
#import <math.h>
static void require(BOOL ok,const char *message) { if (!ok) { fprintf(stderr,"FAIL %s\n",message); exit(1); } }
int main(void) {
    @autoreleasepool {
        UIScrollView *scroll=[[UIScrollView alloc] initWithFrame:CGRectMake(0,0,337,220)];
        scroll.contentSize=CGSizeMake(337,1000);
        scroll.contentOffset=CGPointMake(0,780);
        require(ControllerHistoryAtBottom(scroll),"recognise bottom");
        BOOL follow=ControllerHistoryAtBottom(scroll);
        scroll.contentSize=CGSizeMake(337,1200);
        ControllerHistoryFollow(scroll,follow);
        require(fabs(scroll.contentOffset.y-980)<1,"follow appended output");
        scroll.contentOffset=CGPointMake(0,100);
        require(!ControllerHistoryAtBottom(scroll),"reader scrolled up");
        scroll.contentSize=CGSizeMake(337,1400);
        ControllerHistoryFollow(scroll,NO);
        require(fabs(scroll.contentOffset.y-100)<1,"preserve reader position");
        ControllerHistoryFollow(scroll,YES);
        require(fabs(scroll.contentOffset.y-1180)<1,"explicit Run follows new output");
        scroll.contentSize=CGSizeMake(337,40);
        ControllerHistoryFollow(scroll,YES);
        require(fabs(scroll.contentOffset.y)<1,"short output clamps to top");
        scroll.contentInset=UIEdgeInsetsMake(10,0,20,0);
        scroll.contentSize=CGSizeMake(337,500);
        ControllerHistoryFollow(scroll,YES);
        require(fabs(scroll.contentOffset.y-300)<1,"account for bottom inset");
        UIScrollView *laidOut=[[UIScrollView alloc] initWithFrame:CGRectMake(0,0,337,220)];
        UIView *content=[UIView new];
        content.translatesAutoresizingMaskIntoConstraints=NO;
        [laidOut addSubview:content];
        NSLayoutConstraint *height=[content.heightAnchor constraintEqualToConstant:500];
        [NSLayoutConstraint activateConstraints:@[
            [content.topAnchor constraintEqualToAnchor:laidOut.contentLayoutGuide.topAnchor],
            [content.bottomAnchor constraintEqualToAnchor:laidOut.contentLayoutGuide.bottomAnchor],
            [content.leadingAnchor constraintEqualToAnchor:laidOut.contentLayoutGuide.leadingAnchor],
            [content.trailingAnchor constraintEqualToAnchor:laidOut.contentLayoutGuide.trailingAnchor],
            [content.widthAnchor constraintEqualToAnchor:laidOut.frameLayoutGuide.widthAnchor],height]];
        ControllerHistoryFollow(laidOut,YES);
        require(fabs(laidOut.contentOffset.y-280)<1,"initial Auto Layout content");
        BOOL pinned=ControllerHistoryAtBottom(laidOut);
        height.constant=1000;
        ControllerHistoryFollow(laidOut,pinned);
        require(fabs(laidOut.contentOffset.y-780)<1,"resolve new content height before following");
        puts("History append-follow, scrollback preservation, explicit follow and bounds PASS");
    }
    return 0;
}

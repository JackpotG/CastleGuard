const assert = require('node:assert/strict');
const {test,before} = require('node:test');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const routing = require('./tree-routing.js');
before(async()=>{
 const runtime = execFileSync('python3',[path.join(__dirname,'build_tree_router.py')],{encoding:'utf8'}).trim();
 global.createTreeConnectionRouter = require(runtime);
 await routing.ready();
});
function avoids(rect,a,b) {
 return a[0]===b[0] ? !(a[0]>rect[0]&&a[0]<rect[2]&&Math.min(a[1],b[1])<rect[3]&&Math.max(a[1],b[1])>rect[1]) :
  !(a[1]>rect[1]&&a[1]<rect[3]&&Math.min(a[0],b[0])<rect[2]&&Math.max(a[0],b[0])>rect[0]);
}
function independentInputs(paths,edges) {
 for(let i=0;i<paths.length;i++) for(let j=0;j<i;j++){
  if(edges[i][0]===edges[j][0]) continue;
  if(edges[i][1]===edges[j][1]) assert.notDeepEqual(paths[i].points.at(-1),paths[j].points.at(-1));
  for(const a of paths[i].strokes) for(const b of paths[j].strokes)
   for(let x=1;x<a.length;x++) for(let y=1;y<b.length;y++){
    const p=a[x-1],q=a[x],r=b[y-1],s=b[y];
    const horizontal=Math.abs(p[1]-q[1])<.01,other=Math.abs(r[1]-s[1])<.01;
    if(horizontal!==other) continue;
    const axis=horizontal?0:1,normal=1-axis;
    const overlap=Math.min(Math.max(p[axis],q[axis]),Math.max(r[axis],s[axis]))-Math.max(Math.min(p[axis],q[axis]),Math.min(r[axis],s[axis]));
    assert.ok(Math.abs(p[normal]-r[normal])>=.01||overlap<=.01,'different sources must not share a wire');
   }
 }
}
test('same-parent siblings reuse one trunk and fork in the browser',()=>{
 const nodes=[[125,25,175,75],[45,155,95,205],[205,155,255,205]];
 const paths=routing.plan(nodes,[[0,1],[0,2]],[],[8,8,292,240]);
 for(const route of paths){
  assert.deepEqual(route.points.slice(0,2),[[150,75],[150,115]]);
  assert.equal(route.points[2][1],115);
  assert.equal(route.strokes.length,1);
 }
});
test('a long prerequisite keeps its source bus and a separate incoming port',()=>{
 const nodes=[[125,25,175,75],[365,25,415,75],[125,135,175,185],
              [245,135,295,185],[365,135,415,185],[275,245,325,295]];
 const paths=routing.plan(nodes,[[0,2],[0,3],[1,4],[4,5],[0,5]],[],[8,8,440,330]);
 const link=paths.at(-1);
 assert.ok(link.points.length>=6);
 assert.deepEqual(link.points.slice(1,3).map(p=>p[1]),[paths[1].points[1][1],paths[1].points[1][1]]);
 assert.ok(link.points.at(-2)[1]>185&&link.points.at(-2)[1]<245);
 independentInputs(paths,[[0,2],[0,3],[1,4],[4,5],[0,5]]);
 for(let i=1;i<link.points.length;i++) for(const rect of nodes) assert.ok(avoids(rect,link.points[i-1],link.points[i]));
});
test('four siblings share outgoing trunks but keep mixed-parent inputs separate',()=>{
 const nodes=[[154,31,200,77],[464,31,510,77],[61,140,107,186],
              [245,140,291,186],[351,140,397,186],[464,140,510,186]];
 const labels=[[209,46,233,62],[430,46,454,62],[116,155,140,171],
               [300,155,348,171],[317,155,341,171],[407,155,455,171]];
 const edges=[[0,2],[0,3],[1,5],[0,4],[1,4]];
 const paths=routing.plan(nodes,edges,labels,[8,8,550,260]);
 for(const route of paths){
  assert.ok(route.points.length>=2);
  for(let i=1;i<route.points.length;i++){
   const a=route.points[i-1],b=route.points[i];
   for(const rect of [...nodes,...labels]) assert.ok(avoids(rect,a,b));
  }
 }
 assert.equal(paths[0].points[1][1],paths[1].points[1][1]);
 assert.equal(paths[0].points[1][1],paths[3].points[1][1]);
 assert.notEqual(paths[0].points[1][1],paths[4].points[1][1]);
 independentInputs(paths,edges);
});
test('browser uses the game router to bypass intermediate nodes and labels',()=>{
 const nodes=[[125,25,175,75],[125,135,175,185],[125,245,175,295]], labels=[[105,190,195,210]];
 const paths=routing.plan(nodes,[[0,2],[0,1],[1,2]],labels,[8,8,292,330]);
 assert.ok(paths[0].points.length>2);
 for(const route of paths){
  assert.ok(route.strokes.length);
  for(let i=1;i<route.points.length;i++) for(const rect of [...nodes,...labels]) assert.ok(avoids(rect,route.points[i-1],route.points[i]));
 }
});
test('each dependency survives a dense graph and horizontal defense branches',()=>{
 const nodes=[[25,90,65,130],[125,90,165,130],[225,90,265,130]], edges=[[0,1],[1,2],[0,2]];
 const paths=routing.plan(nodes,edges,[],[8,8,292,220]);
 assert.equal(paths.length,edges.length);
 assert.ok(paths.every(route=>route.points.length>=2&&route.strokes.length));
 independentInputs(paths,edges);
 assert.ok(routing.plan(nodes,[[-1,2],[0,99],[0,0]],[],[8,8,292,220]).every(route=>!route.points.length));
});
test('three prerequisites retain ordered entry ports when config order changes',()=>{
 const nodes=[[20,25,60,65],[130,25,170,65],[240,25,280,65],[130,180,170,220]],edges=[[0,3],[1,3],[2,3]];
 const paths=routing.plan(nodes,edges,[],[8,8,292,250]);
 independentInputs(paths,edges);
 const ports=paths.map(p=>p.points.at(-1));
 assert.ok(ports[0][0]<ports[1][0]&&ports[1][0]<ports[2][0]);
 const reversed=routing.plan(nodes,[...edges].reverse(),[],[8,8,292,250]);
 assert.deepEqual(reversed.map(p=>p.points.at(-1)).reverse(),ports);
});

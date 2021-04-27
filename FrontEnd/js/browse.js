//Vue components


var app_search=new Vue({
    el:"#searchForBook",
    data:{
        searchContent:"",
    },
    methods:{
        search:function(){

            var l=app_browselist.displays.length;
            app_browselist.c=this.searchContent;
            for(var i=0;i<l;i++)
                app_browselist.displays.pop();
            if(this.searchContent=="")
            {
                for(var i=0;i<app_browselist.books.length;i++)
                {   
                    app_browselist.displays.push(app_browselist.books[i]);
                }
            }
            else{
                for(var i=0;i<app_browselist.books.length;i++)
                {   
                    if(app_browselist.books[i].bookname.toLowerCase().match(this.searchContent.toLowerCase())||
                        app_browselist.books[i].author.toLowerCase().match(this.searchContent.toLowerCase())||
                        app_browselist.books[i].isbnnum.toLowerCase().match(this.searchContent.toLowerCase()))
                        { 
							app_browselist.displays.push(app_browselist.books[i]);
						}
				}
			}

        }
    }
})


var app_browselist=new Vue({
    el:"#browseList",
    data:{
		browse_state:true,
        books:[],
        c:"",
        displays:[],
        type:0
    },
    mounted(){
        this.$http.get('http://202.120.40.8:30603').then(function(res){
				console.log('请求成功');
				Object.assign(this.books,res.data);
                console.log('Books:');
				console.log(this.books);
                this.init();
            },function(){
                console.log('请求失败处理');
			});
        
    },
    methods:{
        sort:function(){
            if(this.type==1){
                this.displays=this.displays.sort(comparator1);
            }
            else if (this.type==2){
                this.displays=this.displays.sort(comparator2);
            }
            else if (this.type==3){
                this.displays=this.displays.sort(comparator3);
            }
            else if (this.type==4){
                this.displays=this.displays.sort(comparator4);
            }
        },
        init:function(){
            for(var i=0;i<this.books.length;i++)
            {
                this.displays.push(this.books[i]);

			}
		}
    },
    computed:{
    }
})

var app_main=new Vue({
    el:"#main",
    data:{
        isLoggedIn:false,
		username:null,
		isadmin:false
    },
    mounted(){
    }
})


function comparator1(a,b)
{
    return a.id-b.id;
}
function comparator2(a,b)
{
    return a.author.length-b.author.length;
}
function comparator3(a,b)
{
    return a.storage-b.storage;
}
function comparator4(a,b)
{
    return Number(a.isbnnum)-Number(b.isbnnum);
}

